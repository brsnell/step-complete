// SectionTool tests: which side is cut, flipping, capping, multiple planes,
// handles and dragging, and that only parts are clipped.

#include "RenderTestUtils.h"
#include "TestModels.h"

#include "core/AssemblyIndex.h"
#include "render/OcctViewport.h"
#include "render/SceneManager.h"
#include "render/SectionTool.h"

#include <QtTest>

#include <memory>

using sc::render::SectionPlane;

namespace {

using Px = NCollection_Vec2<int>;

// The fixture's base plate is red (100 x 60 x 10 at the origin).
bool isPlateRed(const QColor& color) {
  return color.red() > 90 && color.red() > color.green() + 50 && color.red() > color.blue() + 50;
}

int countIn(const QImage& image, const QRect& area, bool (*match)(const QColor&)) {
  int count = 0;
  for (int y = area.top(); y <= area.bottom(); ++y) {
    for (int x = area.left(); x <= area.right(); ++x) {
      count += match(image.pixelColor(x, y)) ? 1 : 0;
    }
  }
  return count;
}

} // namespace

class SectionToolTest : public QObject {
  Q_OBJECT

private:
  std::unique_ptr<sc::render::OcctViewport> _viewport;
  std::unique_ptr<sc::render::SceneManager> _scene;
  std::unique_ptr<sc::render::SectionTool> _section;

  QImage render() {
    _viewport->invalidateScene();
    return _viewport->grabFramebuffer();
  }

  // Screen rectangle of a world-space box (x0..x1, y0..y1 at height z), top view.
  QRect screenRect(double x0, double y0, double x1, double y1, double z) const {
    int ax = 0, ay = 0, bx = 0, by = 0;
    _viewport->view()->Convert(x0, y0, z, ax, ay);
    _viewport->view()->Convert(x1, y1, z, bx, by);
    return QRect(QPoint(std::min(ax, bx), std::min(ay, by)), QPoint(std::max(ax, bx), std::max(ay, by)))
        .adjusted(4, 4, -4, -4);
  }

  SectionPlane planeX(double offset) const {
    SectionPlane plane;
    plane.enabled = true;
    plane.normal = gp_Dir(1, 0, 0);
    plane.offset = offset;
    return plane;
  }

private Q_SLOTS:
  void init() {
    _viewport = std::make_unique<sc::render::OcctViewport>();
    _viewport->resize(500, 400);
    _viewport->show();
    QVERIFY(QTest::qWaitForWindowExposed(_viewport.get()));
    _viewport->grabFramebuffer();
    QVERIFY(_viewport->isGlReady());
    auto index = std::make_shared<sc::core::AssemblyIndex>(sc::test::buildFixtureAssembly(false));
    _scene = std::make_unique<sc::render::SceneManager>(_viewport->context());
    _scene->setModel(index);
    _section = std::make_unique<sc::render::SectionTool>(_viewport.get(), _scene.get());
    _section->setBounds(_scene->boundingBox(index->roots()));
    _viewport->setStandardView(V3d_Zpos);
    _viewport->fitAll();
    render();
  }

  void cleanup() {
    _section.reset();
    _scene.reset();
    _viewport.reset();
  }

  void cutsAwayTheSideTheNormalPointsTo() {
    // Plate area away from the pins (pins sit at y = 7..13 on the plate's front).
    const QRect left = screenRect(5, 30, 40, 55, 10);
    const QRect right = screenRect(60, 30, 95, 55, 10);
    const QImage before = render();
    QVERIFY(countIn(before, left, isPlateRed) > left.width() * left.height() / 2);
    QVERIFY(countIn(before, right, isPlateRed) > right.width() * right.height() / 2);

    _section->setPlane(0, planeX(50));
    QVERIFY(_section->isActive());
    const QImage cut = render();
    sc::test::dumpImage(cut, "section_cut_x50");
    QVERIFY2(countIn(cut, left, isPlateRed) > left.width() * left.height() / 2, "the kept side must remain");
    QCOMPARE(countIn(cut, right, isPlateRed), 0);

    _section->flip(0);
    QVERIFY(_section->plane(0).normal.IsEqual(gp_Dir(-1, 0, 0), 1e-12));
    const QImage flipped = render();
    QCOMPARE(countIn(flipped, left, isPlateRed), 0);
    QVERIFY(countIn(flipped, right, isPlateRed) > right.width() * right.height() / 2);

    _section->reset();
    QVERIFY(!_section->isActive());
    QVERIFY(sc::test::changedPixelFraction(before, render()) < 0.001);
  }

  void capsTheCutSolids() {
    // Look at the cut face: plate cut at x = 50, viewed from +X.
    SectionPlane plane = planeX(50);
    plane.capping = false;
    _section->setHandlesVisible(false);
    _section->setPlane(0, plane);
    _viewport->setStandardView(V3d_Xpos);
    _viewport->fitAll();
    const QImage open = render();
    plane.capping = true;
    plane.hatch = false;
    _section->setPlane(0, plane);
    const QImage capped = render();
    sc::test::dumpImage(capped, "section_capped");
    const QRect all = capped.rect();
    QVERIFY2(countIn(capped, all, isPlateRed) > countIn(open, all, isPlateRed) + 100,
             "the cap should fill the cut with the plate's color");

    plane.hatch = true;
    _section->setPlane(0, plane);
    const QImage hatched = render();
    sc::test::dumpImage(hatched, "section_hatched");
    QVERIFY2(sc::test::changedPixelFraction(capped, hatched) > 0.001, "hatching should show on the cap");
  }

  void threePlanesKeepTheirCommonSide() {
    _section->setHandlesVisible(false);
    for (int i = 0; i < sc::render::SectionTool::MaxPlanes; ++i) {
      SectionPlane plane = _section->plane(i);
      plane.enabled = true;
      _section->setPlane(i, plane); // X, Y, Z through the model's center: x = 50, y = 30, z = 15
    }
    QCOMPARE(_section->plane(0).offset, 50.0);
    QCOMPARE(_section->plane(1).offset, 30.0);
    // Each plane removes the side its normal points to, so what remains is x < 50 and y < 30.
    const QRect kept = screenRect(15, 16, 40, 28, 10);
    const QRect cutByX = screenRect(60, 16, 95, 28, 10);
    const QRect cutByY = screenRect(15, 35, 40, 55, 10);
    const QImage image = render();
    sc::test::dumpImage(image, "section_three_planes");
    QVERIFY(countIn(image, kept, isPlateRed) > kept.width() * kept.height() / 2);
    QCOMPARE(countIn(image, cutByX, isPlateRed), 0);
    QCOMPARE(countIn(image, cutByY, isPlateRed), 0);
  }

  void draggingTheHandleMovesThePlane() {
    _viewport->setStandardView(V3d_XposYnegZpos);
    _viewport->fitAll();
    _section->setPlane(0, planeX(50));
    render();
    // Hover the handle at the plane's center, press, drag right (towards +X on screen).
    int pixelX = 0, pixelY = 0;
    _viewport->view()->Convert(50, 30, 5, pixelX, pixelY);
    _viewport->context()->MoveTo(pixelX, pixelY, _viewport->view(), false);
    QVERIFY(_viewport->context()->HasDetected());
    QVERIFY(_section->beginDrag(_viewport->context()->DetectedInteractive(), Px(pixelX, pixelY)));
    QSignalSpy changed(_section.get(), &sc::render::SectionTool::planeChanged);
    _section->drag(Px(pixelX, pixelY), Px(pixelX + 40, pixelY + 20));
    _section->endDrag();
    QVERIFY(changed.count() >= 1);
    QVERIFY2(_section->plane(0).offset > 55.0, "dragging towards +X moves the plane along +X");
    const auto [low, high] = _section->offsetRange(0);
    QVERIFY(_section->plane(0).offset <= high + 1e-9);

    // Dragging far beyond the model clamps to its extent.
    QVERIFY(_section->beginDrag(_viewport->context()->DetectedInteractive(), Px(pixelX, pixelY)));
    _section->drag(Px(pixelX, pixelY), Px(pixelX + 5000, pixelY + 2500));
    _section->endDrag();
    QVERIFY(std::abs(_section->plane(0).offset - high) < 1e-9);
    Q_UNUSED(low);

    // Other objects are not taken as handles.
    QVERIFY(!_section->beginDrag(sc::OccHandle<AIS_InteractiveObject>(), Px(0, 0)));
  }
};

int sc::test::runSectionToolTests(int argc, char** argv) {
  SectionToolTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_section_tool.moc"
