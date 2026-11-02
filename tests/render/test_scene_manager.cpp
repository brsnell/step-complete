// SceneManager tests: instancing, visibility, selection mapping and rendering
// of an assembly built from the shared fixture.

#include "RenderTestUtils.h"
#include "TestModels.h"

#include "core/AssemblyIndex.h"
#include "render/OcctViewport.h"
#include "render/SceneManager.h"

#include <AIS_ListOfInteractive.hxx>

#include <QtTest>

#include <memory>

using sc::OccHandle;
using sc::core::AssemblyIndex;
using sc::core::NodeId;
using sc::render::SceneManager;

namespace {

NodeId findNode(const AssemblyIndex& index, const std::string& name, int occurrence = 0) {
  for (const auto& node : index.nodes()) {
    if (node.name == name && occurrence-- == 0) {
      return node.id;
    }
  }
  return sc::core::kNoNode;
}

const std::string kPin = "Pin \xC3\x98"
                         "6";

// Visible part objects currently displayed by the context.
int displayedParts(const SceneManager& scene, const OccHandle<AIS_InteractiveContext>& context) {
  AIS_ListOfInteractive objects;
  context->DisplayedObjects(objects);
  int count = 0;
  for (AIS_ListOfInteractive::Iterator it(objects); it.More(); it.Next()) {
    if (scene.partOf(it.Value()) != sc::core::kNoNode) {
      ++count;
    }
  }
  return count;
}

} // namespace

class SceneManagerTest : public QObject {
  Q_OBJECT

private:
  std::unique_ptr<sc::render::OcctViewport> m_viewport;
  std::unique_ptr<SceneManager> m_scene;
  std::shared_ptr<AssemblyIndex> m_index;

  QImage render() {
    m_viewport->invalidateScene();
    return m_viewport->grabFramebuffer();
  }

private Q_SLOTS:
  void init() {
    m_viewport = std::make_unique<sc::render::OcctViewport>();
    m_viewport->resize(400, 300);
    m_viewport->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_viewport.get()));
    m_viewport->grabFramebuffer();
    QVERIFY(m_viewport->isGlReady());

    m_index = std::make_shared<AssemblyIndex>(sc::test::buildFixtureAssembly());
    m_scene = std::make_unique<SceneManager>(m_viewport->context());
    m_scene->setModel(m_index);
    m_viewport->fitAll();
  }

  void cleanup() {
    m_scene.reset(); // removes presentations while the context is alive
    m_viewport.reset();
    m_index.reset();
  }

  void sharesPrototypesBetweenInstances() {
    // Plate, plain pin, green pin: 5 instances share 3 presentations.
    QCOMPARE(m_scene->prototypeCount(), 3);
    QCOMPARE(m_scene->detachedCount(), 0);
  }

  void respectsVisibilityFromFile() {
    // The second pin pair is hidden in the document.
    QCOMPARE(displayedParts(*m_scene, m_viewport->context()), 3);
    QVERIFY(!m_scene->isVisible(findNode(*m_index, "Pin pair", 1)));
    QVERIFY(m_scene->isVisible(m_index->roots().front()));
  }

  void hidesIsolatesAndShowsAll() {
    const NodeId plate = findNode(*m_index, "Base plate");
    m_scene->isolate({plate});
    QCOMPARE(displayedParts(*m_scene, m_viewport->context()), 1);
    QVERIFY(m_scene->isVisible(plate));

    m_scene->setVisible({plate}, false);
    QCOMPARE(displayedParts(*m_scene, m_viewport->context()), 0);

    m_scene->showAll();
    QCOMPARE(displayedParts(*m_scene, m_viewport->context()), 5);
  }

  void mapsSelectionBothWays() {
    const NodeId pair = findNode(*m_index, "Pin pair", 0);
    m_scene->select({pair});
    const auto selected = m_scene->selectedParts();
    QCOMPARE(int(selected.size()), 2);
    for (const NodeId id : selected) {
      QCOMPARE(m_index->node(id).name, kPin);
      QCOMPARE(m_index->node(id).parent, pair);
    }
    m_scene->select({});
    QVERIFY(m_scene->selectedParts().empty());
  }

  void computesWorldBoundingBoxes() {
    const NodeId pin = findNode(*m_index, kPin, 1); // (10,10,10) + (20,0,0), r = 3
    const Bnd_Box box = m_scene->boundingBox({pin});
    QVERIFY(!box.IsVoid());
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    QVERIFY2(qAbs((xmin + xmax) / 2 - 30.0) < 0.5, qPrintable(QString::number((xmin + xmax) / 2)));
    QVERIFY(qAbs(zmin - 10.0) < 0.5);
  }

  void detachesInstancesForTransparency() {
    const NodeId pin = findNode(*m_index, kPin, 0);
    m_scene->select({pin});
    m_scene->setTransparency({pin}, 0.6);
    QCOMPARE(m_scene->detachedCount(), 1);
    QVERIFY(m_scene->isVisible(pin));
    QCOMPARE(displayedParts(*m_scene, m_viewport->context()), 3);
    QCOMPARE(m_scene->selectedParts(), std::vector<NodeId>{pin}); // selection survives detaching
  }

  void highlightsSelectedInstances() {
    const NodeId plate = findNode(*m_index, "Base plate");
    const QImage before = render();
    m_scene->select({plate});
    const QImage after = render();
    sc::test::dumpImage(after, "scene_selected_plate");
    const double changed = sc::test::changedPixelFraction(before, after);
    qInfo("selection changed %.1f%% of pixels", changed * 100.0);
    QVERIFY2(changed > 0.15, "selected part is not visibly highlighted");

    // ... and in the selection color (blue), not OCCT's default gray.
    const QColor center = after.pixelColor(after.width() / 2, after.height() * 2 / 3);
    QVERIFY2(center.blue() > center.red() + 60, qPrintable(center.name()));
  }

  void rendersAssemblyAndSwitchesStyles() {
    const QImage shadedEdges = render();
    sc::test::dumpImage(shadedEdges, "scene_shaded_edges");

    // Something rendered, including the green instance color.
    int green = 0;
    for (int y = 0; y < shadedEdges.height(); ++y) {
      for (int x = 0; x < shadedEdges.width(); ++x) {
        const QColor c = shadedEdges.pixelColor(x, y);
        if (c.green() > 120 && c.green() > c.red() + 60 && c.green() > c.blue() + 60) {
          ++green;
        }
      }
    }
    QVERIFY2(green > 20, "instance color (green pin) is not rendered");

    m_scene->setDisplayStyle(sc::render::DisplayStyle::Wireframe);
    const QImage wireframe = render();
    sc::test::dumpImage(wireframe, "scene_wireframe");
    QVERIFY2(sc::test::changedPixelFraction(shadedEdges, wireframe) > 0.05, "wireframe looks like shaded");

    m_scene->setDisplayStyle(sc::render::DisplayStyle::Shaded);
    const QImage shaded = render();
    QVERIFY2(sc::test::changedPixelFraction(shadedEdges, shaded) > 0.002,
             "edges are not toggled on instances");
  }
};

int sc::test::runSceneManagerTests(int argc, char** argv) {
  SceneManagerTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_scene_manager.moc"
