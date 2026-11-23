// ThicknessScene (Measure > Wall Thickness, docs/designs/wall-thickness.md): the colored parts
// stand in for the parts without losing visibility or selection, the colors follow the limits,
// and the readout looks into the part under a point.

#include "RenderTestUtils.h"
#include "TestModels.h"

#include "core/AssemblyIndex.h"
#include "core/WallThickness.h"
#include "render/AisCompat.h"
#include "render/OcctViewport.h"
#include "render/SceneManager.h"
#include "render/ThicknessScene.h"

#include <BRepMesh_IncrementalMesh.hxx>

#include <QtTest>

#include <memory>

using sc::OccHandle;
using sc::core::AssemblyIndex;
using sc::core::NodeId;
using sc::render::SceneManager;
using sc::render::ThicknessScene;

namespace {

NodeId findNode(const AssemblyIndex& index, const std::string& name) {
  for (const auto& node : index.nodes()) {
    if (node.name == name) {
      return node.id;
    }
  }
  return sc::core::NoNode;
}

OccHandle<AIS_InteractiveObject> objectOf(const SceneManager& scene,
                                          const OccHandle<AIS_InteractiveContext>& context, NodeId part) {
  sc::occ::InteractiveList objects;
  context->DisplayedObjects(objects);
  for (sc::occ::InteractiveList::Iterator it(objects); it.More(); it.Next()) {
    if (scene.partOf(it.Value()) == part) {
      return it.Value();
    }
  }
  return nullptr;
}

} // namespace

class ThicknessSceneTest : public QObject {
  Q_OBJECT

private:
  std::unique_ptr<sc::render::OcctViewport> _viewport;
  std::unique_ptr<SceneManager> _scene;
  std::unique_ptr<ThicknessScene> _thickness;
  std::shared_ptr<AssemblyIndex> _index;

  QImage render() {
    _viewport->invalidateScene();
    return _viewport->grabFramebuffer();
  }

private Q_SLOTS:
  void init() {
    _viewport = std::make_unique<sc::render::OcctViewport>();
    _viewport->resize(500, 400);
    _viewport->show();
    QVERIFY(QTest::qWaitForWindowExposed(_viewport.get()));
    _viewport->grabFramebuffer();
    QVERIFY(_viewport->isGlReady());
    _index = std::make_shared<AssemblyIndex>(sc::test::buildFixtureAssembly());
    for (const NodeId part : _index->partInstances()) {
      BRepMesh_IncrementalMesh(_index->prototypeShape(part), 0.05);
    }
    _scene = std::make_unique<SceneManager>(_viewport->context());
    _scene->setModel(_index);
    _thickness = std::make_unique<ThicknessScene>(_viewport->context(), *_scene);
    _viewport->fitAll();
  }

  void cleanup() {
    _thickness.reset();
    _scene.reset();
    _viewport.reset();
    _index.reset();
  }

  void colorsTheVisiblePartsAndRestoresThem() {
    std::vector<NodeId> visible;
    for (const NodeId part : _index->partInstances()) {
      if (_scene->isVisible(part)) {
        visible.push_back(part);
      }
    }
    QCOMPARE(visible.size(), size_t(3)); // the plate and one pin pair
    const QImage before = render();

    auto result = std::make_shared<sc::core::ThicknessResult>(sc::core::analyseThickness(*_index, visible));
    _thickness->show(result, *_index);
    QVERIFY(_thickness->isShown());
    QCOMPARE(_thickness->presentationCount(), 3);
    QVERIFY(_thickness->hasLegend());
    const NodeId plate = findNode(*_index, "Base plate");
    QVERIFY(_scene->hasOverride(plate));
    QVERIFY(_scene->isVisible(plate));

    // Pins (6 mm) red, the plate's 60 and 100 mm sides blue.
    _thickness->setLimits(6.0, 20.0);
    const QImage colored = render();
    sc::test::dumpImage(colored, "thickness_view");
    int red = 0;
    int blue = 0;
    for (int y = 0; y < colored.height(); ++y) {
      for (int x = 0; x < colored.width(); ++x) {
        const QColor color = colored.pixelColor(x, y);
        red += color.red() > 150 && color.green() < 90 && color.blue() < 90 ? 1 : 0;
        blue += color.blue() > 150 && color.red() < 90 && color.green() < 120 ? 1 : 0;
      }
    }
    QVERIFY2(red > 200, qPrintable(QStringLiteral("red pixels: %1").arg(red)));
    QVERIFY2(blue > 2000, qPrintable(QStringLiteral("blue pixels: %1").arg(blue)));

    // Selecting, hiding and the readout go through the colored stand-in.
    _scene->select({plate});
    QCOMPARE(_scene->selectedParts(), std::vector<NodeId>{plate});
    const OccHandle<AIS_InteractiveObject> object = objectOf(*_scene, _viewport->context(), plate);
    QVERIFY(!object.IsNull());
    const std::optional<double> top = _thickness->thicknessAt(object, gp_Pnt(50, 30, 10), gp_Dir(0, 0, -1));
    QVERIFY(top);
    QCOMPARE(qRound(*top * 1000), 10000);
    _scene->setVisible({plate}, false);
    QVERIFY(!_scene->isVisible(plate));
    _scene->setVisible({plate}, true);

    // Off: the parts' own presentations are back and the picture is as before.
    _thickness->clear();
    QVERIFY(!_thickness->isShown());
    QVERIFY(!_scene->hasOverride(plate));
    QVERIFY(!_thickness->hasLegend());
    _scene->select({});
    const double changed = sc::test::changedPixelFraction(before, render());
    QVERIFY2(changed < 0.001, qPrintable(QStringLiteral("changed %1").arg(changed)));
  }

  void colorScale() {
    const auto srgb = [](const Quantity_Color& color) {
      const NCollection_Vec3<float> c = Quantity_Color::Convert_LinearRGB_To_sRGB(
          NCollection_Vec3<float>(float(color.Red()), float(color.Green()), float(color.Blue())));
      return QColor::fromRgbF(c.r(), c.g(), c.b());
    };
    QCOMPARE(srgb(ThicknessScene::colorOf(1.0, 2.0, 4.0)), QColor(255, 0, 0)); // thinner than the limit
    QCOMPARE(srgb(ThicknessScene::colorOf(9.0, 2.0, 4.0)), QColor(0, 0, 255)); // thicker
    QCOMPARE(srgb(ThicknessScene::colorOf(3.0, 2.0, 4.0)), QColor(0, 255, 0)); // halfway
    const QColor none = srgb(ThicknessScene::colorOf(std::nan(""), 2.0, 4.0));
    QCOMPARE(none.red(), none.blue()); // grey
  }
};

int sc::test::runThicknessSceneTests(int argc, char** argv) {
  ThicknessSceneTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_thickness_scene.moc"
