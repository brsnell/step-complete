// ComparisonScene (File > Compare With, docs/designs/model-comparison.md): changed parts colored
// by deviation, removed parts as ghosts, a legend, and everything back to normal afterwards.

#include "RenderTestUtils.h"

#include "core/AssemblyIndex.h"
#include "core/Comparison.h"
#include "render/ComparisonScene.h"
#include "render/OcctViewport.h"
#include "render/SceneManager.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <TDataStd_Name.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <QtTest>

#include <memory>

using sc::OccHandle;
using sc::core::AssemblyIndex;
using sc::render::ComparisonScene;
using sc::render::SceneManager;

namespace {

TopoDS_Shape meshed(const TopoDS_Shape& shape) {
  BRepMesh_IncrementalMesh(shape, 0.05);
  return shape;
}

// A plate of the given thickness and, optionally, a "Clip" cube beside it.
std::shared_ptr<AssemblyIndex> revision(double thickness, bool clip) {
  auto document = std::make_shared<sc::core::Document>();
  const OccHandle<XCAFDoc_ShapeTool> shapes = XCAFDoc_DocumentTool::ShapeTool(document->ocaf()->Main());
  TDataStd_Name::Set(shapes->AddShape(meshed(BRepPrimAPI_MakeBox(100.0, 60.0, thickness).Shape()), false),
                     "Base plate");
  if (clip) {
    TDataStd_Name::Set(
        shapes->AddShape(meshed(BRepPrimAPI_MakeBox(gp_Pnt(0, 70, 0), 20.0, 20.0, 20.0).Shape()), false),
        "Clip");
  }
  return std::make_shared<AssemblyIndex>(document);
}

} // namespace

class ComparisonSceneTest : public QObject {
  Q_OBJECT

private:
  std::unique_ptr<sc::render::OcctViewport> _viewport;
  std::unique_ptr<SceneManager> _scene;
  std::unique_ptr<ComparisonScene> _comparison;

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
    _scene = std::make_unique<SceneManager>(_viewport->context());
    _comparison = std::make_unique<ComparisonScene>(_viewport->context(), *_scene);
  }

  void cleanup() {
    _comparison.reset();
    _scene.reset();
    _viewport.reset();
  }

  void colorsChangesShowsRemovedPartsAndRestores() {
    const auto current = revision(12.0, false); // the plate grew 2 mm, the clip was removed
    const auto other = revision(10.0, true);
    _scene->setModel(current);
    _viewport->fitAll();
    const QImage before = render();

    auto result = std::make_shared<sc::core::ComparisonResult>(sc::core::compareModels(*current, *other));
    QCOMPARE(result->changed, 1);
    QCOMPARE(result->removed, 1);
    _comparison->show(result, *current, other, 0.01);
    QVERIFY(_comparison->isShown());
    QCOMPARE(_comparison->overrideCount(), 1);
    QCOMPARE(_comparison->ghostCount(), 1);
    QVERIFY(_comparison->hasLegend());
    const sc::core::NodeId plate = current->partInstances().front();
    QVERIFY(_scene->hasOverride(plate));
    QVERIFY(!_comparison->ghostBox(1).IsVoid());
    _scene->select({plate});
    QCOMPARE(_scene->selectedParts(), std::vector<sc::core::NodeId>{plate});
    _scene->select({});

    // The plate's top is 2 mm proud of the other revision: red; its sides are green.
    _viewport->fitAll();
    const QImage colored = render();
    sc::test::dumpImage(colored, "comparison_view");
    int red = 0;
    int green = 0;
    for (int y = 0; y < colored.height(); ++y) {
      for (int x = 0; x < colored.width(); ++x) {
        const QColor color = colored.pixelColor(x, y);
        red += color.red() > 150 && color.green() < 90 && color.blue() < 90 ? 1 : 0;
        green += color.green() > 150 && color.red() < 120 && color.blue() < 120 ? 1 : 0;
      }
    }
    QVERIFY2(red > 2000, qPrintable(QStringLiteral("red pixels: %1").arg(red)));
    QVERIFY2(green > 200, qPrintable(QStringLiteral("green pixels: %1").arg(green)));

    _comparison->clear();
    QVERIFY(!_comparison->isShown());
    QVERIFY(!_scene->hasOverride(plate));
    QCOMPARE(_comparison->ghostCount(), 0);
    QVERIFY(!_comparison->hasLegend());
    _viewport->fitAll(); // the ghost widened the fit: frame the plate as before
    const double changed = sc::test::changedPixelFraction(before, render());
    QVERIFY2(changed < 0.001, qPrintable(QStringLiteral("changed %1").arg(changed)));
  }

  void deviationColors() {
    const auto hue = [](const Quantity_Color& color) { return color.Hue(); };
    QCOMPARE(qRound(hue(ComparisonScene::deviationColor(0.005, 0.01, 1.0))), 120); // within: green
    QCOMPARE(qRound(hue(ComparisonScene::deviationColor(1.0, 0.01, 1.0))), 0);     // added: red
    QCOMPARE(qRound(hue(ComparisonScene::deviationColor(-1.0, 0.01, 1.0))), 240);  // removed: blue
  }
};

int sc::test::runComparisonSceneTests(int argc, char** argv) {
  ComparisonSceneTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_comparison_scene.moc"
