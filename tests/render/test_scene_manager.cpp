// SceneManager tests: instancing, visibility, selection mapping and rendering
// of an assembly built from the shared fixture.

#include "RenderTestUtils.h"
#include "TestModels.h"

#include "core/AssemblyIndex.h"
#include "render/AisCompat.h"
#include "render/OcctViewport.h"
#include "render/SceneManager.h"

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
  return sc::core::NoNode;
}

const std::string Pin = "Pin \xC3\x98"
                        "6";

// Visible part objects currently displayed by the context.
int displayedParts(const SceneManager& scene, const OccHandle<AIS_InteractiveContext>& context) {
  sc::occ::InteractiveList objects;
  context->DisplayedObjects(objects);
  int count = 0;
  for (sc::occ::InteractiveList::Iterator it(objects); it.More(); it.Next()) {
    if (scene.partOf(it.Value()) != sc::core::NoNode) {
      ++count;
    }
  }
  return count;
}

} // namespace

class SceneManagerTest : public QObject {
  Q_OBJECT

private:
  std::unique_ptr<sc::render::OcctViewport> _viewport;
  std::unique_ptr<SceneManager> _scene;
  std::shared_ptr<AssemblyIndex> _index;

  QImage render() {
    _viewport->invalidateScene();
    return _viewport->grabFramebuffer();
  }

private Q_SLOTS:
  void init() {
    _viewport = std::make_unique<sc::render::OcctViewport>();
    _viewport->resize(400, 300);
    _viewport->show();
    QVERIFY(QTest::qWaitForWindowExposed(_viewport.get()));
    _viewport->grabFramebuffer();
    QVERIFY(_viewport->isGlReady());

    _index = std::make_shared<AssemblyIndex>(sc::test::buildFixtureAssembly());
    _scene = std::make_unique<SceneManager>(_viewport->context());
    _scene->setModel(_index);
    _viewport->fitAll();
  }

  void cleanup() {
    _scene.reset(); // removes presentations while the context is alive
    _viewport.reset();
    _index.reset();
  }

  void sharesPrototypesBetweenInstances() {
    // Plate, plain pin, green pin: 5 instances share 3 presentations.
    QCOMPARE(_scene->prototypeCount(), 3);
    QCOMPARE(_scene->detachedCount(), 0);
  }

  void respectsVisibilityFromFile() {
    // The second pin pair is hidden in the document.
    QCOMPARE(displayedParts(*_scene, _viewport->context()), 3);
    QVERIFY(!_scene->isVisible(findNode(*_index, "Pin pair", 1)));
    QVERIFY(_scene->isVisible(_index->roots().front()));
  }

  void hidesIsolatesAndShowsAll() {
    const NodeId plate = findNode(*_index, "Base plate");
    _scene->isolate({plate});
    QCOMPARE(displayedParts(*_scene, _viewport->context()), 1);
    QVERIFY(_scene->isVisible(plate));

    _scene->setVisible({plate}, false);
    QCOMPARE(displayedParts(*_scene, _viewport->context()), 0);

    _scene->showAll();
    QCOMPARE(displayedParts(*_scene, _viewport->context()), 5);
  }

  void mapsSelectionBothWays() {
    const NodeId pair = findNode(*_index, "Pin pair", 0);
    _scene->select({pair});
    const auto selected = _scene->selectedParts();
    QCOMPARE(int(selected.size()), 2);
    for (const NodeId id : selected) {
      QCOMPARE(_index->node(id).name, Pin);
      QCOMPARE(_index->node(id).parent, pair);
    }
    _scene->select({});
    QVERIFY(_scene->selectedParts().empty());
  }

  void computesWorldBoundingBoxes() {
    const NodeId pin = findNode(*_index, Pin, 1); // (10,10,10) + (20,0,0), r = 3
    const Bnd_Box box = _scene->boundingBox({pin});
    QVERIFY(!box.IsVoid());
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    QVERIFY2(qAbs((xmin + xmax) / 2 - 30.0) < 0.5, qPrintable(QString::number((xmin + xmax) / 2)));
    QVERIFY(qAbs(zmin - 10.0) < 0.5);
  }

  void detachesInstancesForTransparency() {
    const NodeId pin = findNode(*_index, Pin, 0);
    _scene->select({pin});
    _scene->setTransparency({pin}, 0.6);
    QCOMPARE(_scene->detachedCount(), 1);
    QVERIFY(_scene->isVisible(pin));
    QCOMPARE(displayedParts(*_scene, _viewport->context()), 3);
    QCOMPARE(_scene->selectedParts(), std::vector<NodeId>{pin}); // selection survives detaching
  }

  void highlightsSelectedInstances() {
    const NodeId plate = findNode(*_index, "Base plate");
    const QImage before = render();
    _scene->select({plate});
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
        const QColor color = shadedEdges.pixelColor(x, y);
        if (color.green() > 120 && color.green() > color.red() + 60 && color.green() > color.blue() + 60) {
          ++green;
        }
      }
    }
    QVERIFY2(green > 20, "instance color (green pin) is not rendered");

    _scene->setDisplayStyle(sc::render::DisplayStyle::Wireframe);
    const QImage wireframe = render();
    sc::test::dumpImage(wireframe, "scene_wireframe");
    QVERIFY2(sc::test::changedPixelFraction(shadedEdges, wireframe) > 0.05, "wireframe looks like shaded");

    _scene->setDisplayStyle(sc::render::DisplayStyle::Shaded);
    const QImage shaded = render();
    QVERIFY2(sc::test::changedPixelFraction(shadedEdges, shaded) > 0.002,
             "edges are not toggled on instances");
  }

  void hiddenLineDrawsWhiteFacesAndRestoresColors() {
    std::vector<NodeId> shown;
    for (const NodeId part : _index->partInstances()) {
      if (_scene->isVisible(part)) {
        shown.push_back(part);
      }
    }
    _scene->setVisible(_index->partInstances(), false);
    const QImage background = render();
    _scene->setVisible(shown, true);
    // A transparent part is detached from its prototype; it must follow the style too.
    _scene->setTransparency({shown.back()}, 0.6);
    const QImage before = render();

    _scene->setDisplayStyle(sc::render::DisplayStyle::HiddenLine);
    const QImage hiddenLine = render();
    sc::test::dumpImage(hiddenLine, "scene_hidden_line");

    // Over the parts (pixels that differ from the empty background) there is no
    // part color left: only white faces and dark lines.
    int partPixels = 0;
    int white = 0;
    int colored = 0;
    for (int y = 0; y < hiddenLine.height(); ++y) {
      for (int x = 0; x < hiddenLine.width(); ++x) {
        const QColor color = hiddenLine.pixelColor(x, y);
        const QColor empty = background.pixelColor(x, y);
        if (qAbs(color.red() - empty.red()) + qAbs(color.green() - empty.green()) +
                qAbs(color.blue() - empty.blue()) <=
            24) {
          continue;
        }
        ++partPixels;
        if (color.red() > 245 && color.green() > 245 && color.blue() > 245) {
          ++white;
        }
        if (color.saturation() > 80 && color.value() > 80) {
          ++colored;
        }
      }
    }
    qInfo("hidden line: %d part pixels, %d white, %d colored", partPixels, white, colored);
    QVERIFY2(partPixels > 2000, "parts are not drawn in hidden-line mode");
    QVERIFY2(white > partPixels / 2, "faces are not white in hidden-line mode");
    QVERIFY2(colored < partPixels / 100, "part colors still show in hidden-line mode");

    // Leaving hidden line brings back the file's colors and the lit shading.
    _scene->setDisplayStyle(sc::render::DisplayStyle::ShadedWithEdges);
    const QImage after = render();
    sc::test::dumpImage(after, "scene_after_hidden_line");
    const double changed = sc::test::changedPixelFraction(before, after);
    qInfo("shaded with edges before vs after hidden line: %.2f%% changed", changed * 100.0);
    QVERIFY2(changed < 0.001, "colors or shading are not restored after hidden line");
  }
};

int sc::test::runSceneManagerTests(int argc, char** argv) {
  SceneManagerTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_scene_manager.moc"
