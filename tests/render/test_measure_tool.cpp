// MeasureTool tests: real picks through OCCT's selector on the fixture assembly,
// in a live viewport, down to the measured values and the drawn witness.

#include "RenderTestUtils.h"
#include "TestModels.h"

#include "core/AssemblyIndex.h"
#include "render/MeasureTool.h"
#include "render/OcctViewport.h"
#include "render/SceneManager.h"
#include "render/SectionTool.h"

#include <QtTest>

#include <memory>

using sc::core::Measurement;
using sc::core::MeasureMode;

class MeasureToolTest : public QObject {
  Q_OBJECT

private:
  std::unique_ptr<sc::render::OcctViewport> _viewport;
  std::unique_ptr<sc::render::SceneManager> _scene;
  std::unique_ptr<sc::render::MeasureTool> _tool;
  std::unique_ptr<sc::render::SectionTool> _section;
  std::vector<Measurement> _results;

  // A section view with one plane through 'point', cutting away the side 'normal' points to.
  void sectionThrough(const gp_Pnt& point, const gp_Dir& normal, bool capping = true) {
    _section = std::make_unique<sc::render::SectionTool>(_viewport.get(), _scene.get());
    _section->setBounds(_scene->boundingBox(_scene->model()->roots()));
    sc::render::SectionPlane plane;
    plane.enabled = true;
    plane.normal = normal;
    plane.offset = gp_Vec(point.XYZ()).Dot(gp_Vec(normal));
    plane.capping = capping;
    _section->setPlane(0, plane);
    render();
  }

  void hover(const gp_Pnt& point) {
    int pixelX = 0;
    int pixelY = 0;
    _viewport->view()->Convert(point.X(), point.Y(), point.Z(), pixelX, pixelY);
    _viewport->context()->MoveTo(pixelX, pixelY, _viewport->view(), false);
    _tool->onHoverUpdated();
  }

  QImage render() {
    _viewport->invalidateScene();
    return _viewport->grabFramebuffer();
  }

  // Hovers and clicks the model at a world point, like a user would.
  bool clickAt(const gp_Pnt& point) {
    const auto& view = _viewport->view();
    const auto& context = _viewport->context();
    int pixelX = 0;
    int pixelY = 0;
    view->Convert(point.X(), point.Y(), point.Z(), pixelX, pixelY);
    context->MoveTo(pixelX, pixelY, view, false);
    if (!context->HasDetected()) {
      return false;
    }
    context->SelectDetected(AIS_SelectionScheme_XOR);
    _tool->onSelectionChanged();
    return true;
  }

  static std::string detail(const Measurement& measurement, const std::string& key) {
    for (const auto& [k, v] : measurement.details) {
      if (k == key) {
        return v;
      }
    }
    return {};
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
    _tool = std::make_unique<sc::render::MeasureTool>(_viewport.get(), _scene.get());
    _results.clear();
    connect(_tool.get(), &sc::render::MeasureTool::measured, this,
            [this](const Measurement& m) { _results.push_back(m); });
    _viewport->setStandardView(V3d_Zpos);
    _viewport->fitAll();
    render();
  }

  void cleanup() {
    _tool.reset();
    _section.reset();
    _scene.reset();
    _viewport.reset();
  }

  void distanceBetweenFacesOfDifferentInstances() {
    _tool->setMode(MeasureMode::Distance);
    // Top face of the base plate (z = 10), away from the pins, then the top face of
    // the first pin (pin pair at (10,10,10), pin height 20: top at z = 30).
    QVERIFY(clickAt(gp_Pnt(60, 40, 10)));
    QCOMPARE(_tool->pickCount(), 1);
    QVERIFY(_results.empty());
    QVERIFY(clickAt(gp_Pnt(10, 10, 30)));

    QCOMPARE(_results.size(), size_t(1));
    const Measurement& measurement = _results.front();
    QVERIFY2(measurement.ok, measurement.error.c_str());
    QCOMPARE(measurement.headline, std::string("20.000 mm"));
    QCOMPARE(detail(measurement, "Parallel distance"), std::string("20.000 mm"));
    QCOMPARE(measurement.entities.size(), size_t(2));
    QCOMPARE(measurement.entities[0], std::string("Face (Plane) on Fixture / Base plate"));
    QCOMPARE(_tool->pickCount(), 0);
    QVERIFY(!measurement.witness.empty());
  }

  void radiusOfAPlacedInstance() {
    // The second pin of the second pair sits at (50,10,10) + (20,0,0): only correct
    // if the instance placement is applied to the picked prototype edge.
    _tool->setMode(MeasureMode::Radius);
    QVERIFY(clickAt(gp_Pnt(73, 10, 30))); // on the top circle, radius 3 around (70,10)
    QCOMPARE(_results.size(), size_t(1));
    const Measurement& measurement = _results.front();
    QVERIFY2(measurement.ok, measurement.error.c_str());
    QCOMPARE(detail(measurement, "Radius"), std::string("3.000 mm"));
    QCOMPARE(detail(measurement, "Center"), std::string("70.000, 10.000, 30.000 mm"));
  }

  void clickingAPickAgainUnpicksIt() {
    _tool->setMode(MeasureMode::Angle);
    QVERIFY(clickAt(gp_Pnt(60, 40, 10)));
    QCOMPARE(_tool->pickCount(), 1);
    QVERIFY(clickAt(gp_Pnt(60, 40, 10)));
    QCOMPARE(_tool->pickCount(), 0);
    QVERIFY(_results.empty());
  }

  void witnessIsDrawnOnTop() {
    const QImage before = render();
    _tool->setMode(MeasureMode::Distance);
    QVERIFY(clickAt(gp_Pnt(60, 40, 10)));
    QVERIFY(clickAt(gp_Pnt(10, 10, 30)));
    QCOMPARE(_results.size(), size_t(1));
    _tool->setWitnesses({{1, _results.front().witness, true}});
    const QImage after = render();
    sc::test::dumpImage(after, "measure_witness");

    int orange = 0;
    for (int y = 0; y < after.height(); ++y) {
      for (int x = 0; x < after.width(); ++x) {
        const QColor color = after.pixelColor(x, y);
        if (color.red() > 200 && color.green() > 70 && color.green() < 170 && color.blue() < 80) {
          ++orange;
        }
      }
    }
    QVERIFY2(orange > 10, "witness geometry is not visible");
    QVERIFY(sc::test::changedPixelFraction(before, after) > 0.0005);

    _tool->clearWitnesses();
    QVERIFY2(sc::test::changedPixelFraction(before, render()) < 0.0005, "witness was not removed");
  }

  void severalWitnessesAtOnce() {
    _tool->setMode(MeasureMode::Distance);
    QVERIFY(clickAt(gp_Pnt(60, 40, 10)));
    QVERIFY(clickAt(gp_Pnt(10, 10, 30)));
    _tool->setMode(MeasureMode::Radius);
    QVERIFY(clickAt(gp_Pnt(73, 10, 30)));
    QCOMPARE(_results.size(), size_t(2));
    QCOMPARE(_tool->witnessCount(), 0); // the tool draws only what it is told to

    const QImage before = render();
    _tool->setWitnesses({{7, _results[0].witness, false}, {9, _results[1].witness, true}});
    QCOMPARE(_tool->witnessCount(), 2);
    QVERIFY(!_tool->isEmphasized(7));
    QVERIFY(_tool->isEmphasized(9));
    const QImage both = render();
    sc::test::dumpImage(both, "measure_two_witnesses");

    // Moving the emphasis restyles in place; dropping an id removes just that one.
    _tool->setWitnesses({{7, _results[0].witness, true}, {9, _results[1].witness, false}});
    QVERIFY(_tool->isEmphasized(7));
    QVERIFY(!_tool->isEmphasized(9));
    _tool->setWitnesses({{9, _results[1].witness, true}});
    QCOMPARE(_tool->witnessCount(), 1);
    QVERIFY(!_tool->hasWitness(7));
    const QImage one = render();
    QVERIFY2(sc::test::changedPixelFraction(one, both) > 0.0002, "removed witness is still drawn");

    _tool->setWitnesses({});
    QCOMPARE(_tool->witnessCount(), 0);
    QVERIFY2(sc::test::changedPixelFraction(before, render()) < 0.0005, "witnesses were not removed");
  }

  void resetDropsWitnesses() {
    _tool->setMode(MeasureMode::EntityInfo);
    QVERIFY(clickAt(gp_Pnt(60, 40, 10)));
    _tool->setWitnesses({{1, _results.front().witness, true}});
    _tool->reset();
    QCOMPARE(_tool->witnessCount(), 0);
  }

  // Generated by /ship test coverage audit.
  // Value: protects=setWitnesses skips an empty witness and drops an id whose witness became empty;
  // fails_when=the empty-witness skip or the wanted() emptiness check is removed; why_new=no test
  // passes an empty witness to setWitnesses; seam=none; no_claim=pixel styling of what is drawn
  void emptyWitnessesAreNotDrawn() {
    _tool->setMode(MeasureMode::EntityInfo);
    QVERIFY(clickAt(gp_Pnt(60, 40, 10)));
    QVERIFY(!_results.front().witness.empty());
    _tool->setWitnesses({{1, sc::core::Witness{}, true}});
    QCOMPARE(_tool->witnessCount(), 0);
    _tool->setWitnesses({{1, _results.front().witness, true}, {2, sc::core::Witness{}, false}});
    QCOMPARE(_tool->witnessCount(), 1);
    QVERIFY(!_tool->hasWitness(2));
    _tool->setWitnesses({{1, sc::core::Witness{}, true}});
    QVERIFY2(!_tool->hasWitness(1), "a result whose witness became empty is still drawn");
  }

  void pointToPointShowsWhereAClickSnaps() {
    _tool->setMode(MeasureMode::PointToPoint);
    const auto hover = [this](const gp_Pnt& point) {
      int pixelX = 0;
      int pixelY = 0;
      _viewport->view()->Convert(point.X(), point.Y(), point.Z(), pixelX, pixelY);
      _viewport->context()->MoveTo(pixelX, pixelY, _viewport->view(), false);
      _tool->onHoverUpdated();
    };

    // On the rim of the first pin's top circle (away from its seam vertex at
    // (13,10,30)): snaps to the circle's center.
    hover(gp_Pnt(10, 13, 30));
    QVERIFY(_viewport->overlayMarker().has_value());
    QCOMPARE(_viewport->overlayMarker()->caption, QStringLiteral("Center"));
    QVERIFY(_viewport->overlayMarker()->point.IsEqual(gp_Pnt(10, 10, 30), 1e-6));

    // On the plate's top face, away from edges: the point under the cursor.
    hover(gp_Pnt(60, 40, 10));
    QCOMPARE(_viewport->overlayMarker()->caption, QStringLiteral("On face"));
    QVERIFY(_viewport->overlayMarker()->point.Distance(gp_Pnt(60, 40, 10)) < 1.0);

    // The marker is drawn over the 3D view (composited: grab the widget, not the GL framebuffer).
    render();
    const QImage image = _viewport->grab().toImage().convertToFormat(QImage::Format_RGB32);
    sc::test::dumpImage(image, "measure_snap_marker");
    int orange = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const QColor color = image.pixelColor(x, y);
        if (color.red() > 200 && color.green() > 80 && color.green() < 160 && color.blue() < 60) {
          ++orange;
        }
      }
    }
    QVERIFY2(orange > 20, "snap marker is not drawn");
    // With the cursor off the model (no hover highlight), the scene renders in
    // its own colors: the overlay does not disturb OCCT's OpenGL state.
    _viewport->context()->MoveTo(2, 2, _viewport->view(), false);
    render();
    const QColor plate = _viewport->grab().toImage().pixelColor(20, image.height() / 2);
    QVERIFY2(plate.red() > 120 && plate.green() < 80,
             "model must still render in its colors under the overlay");

    // Other modes don't show it, and leaving Point to Point clears it.
    _tool->setMode(MeasureMode::Distance);
    QVERIFY(!_viewport->overlayMarker().has_value());
    hover(gp_Pnt(10, 13, 30));
    QVERIFY(!_viewport->overlayMarker().has_value());
  }

  // --- Section views (docs/designs/section-aware-measurement.md) ---------------------------

  void aClickOnACapMeasuresTheSectionFace() {
    // The plate is cut at z = 5 and everything above is gone; from the top the cap fills the
    // plate's outline. The plane's handle lies over the whole model: the click goes through it.
    sectionThrough(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1));
    _tool->setMode(MeasureMode::EntityInfo);
    QVERIFY(clickAt(gp_Pnt(60, 40, 5)));
    QCOMPARE(_results.size(), size_t(1));
    const Measurement& measurement = _results.front();
    QVERIFY2(measurement.ok, measurement.error.c_str());
    QCOMPARE(measurement.headline, std::string("6000.000 mm\xC2\xB2"));
    QCOMPARE(measurement.entities.front(), std::string("Section face on Fixture / Base plate"));
  }

  void aPendingSectionFaceIsHighlighted() {
    sectionThrough(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1));
    _tool->setMode(MeasureMode::Distance);
    const QImage before = render();
    QVERIFY(clickAt(gp_Pnt(60, 40, 5)));
    QCOMPARE(_tool->pickCount(), 1);
    QCOMPARE(_tool->sectionFacePickCount(), 1);
    _viewport->context()->MoveTo(2, 2, _viewport->view(), false); // no hover highlight
    const QImage after = render();
    sc::test::dumpImage(after, "measure_section_pick");
    QVERIFY2(sc::test::changedPixelFraction(before, after) > 0.1, "the picked cut face is not drawn");

    // Distance from the cut face to the plate's bottom, seen from below.
    _viewport->setStandardView(V3d_Zneg);
    _viewport->fitAll();
    render();
    QVERIFY(clickAt(gp_Pnt(60, 40, 0)));
    QCOMPARE(_results.size(), size_t(1));
    QCOMPARE(detail(_results.front(), "Parallel distance"), std::string("5.000 mm"));
    QCOMPARE(_tool->sectionFacePickCount(), 0);
  }

  void clickingACapAgainUnpicksIt() {
    sectionThrough(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1));
    _tool->setMode(MeasureMode::Distance);
    QVERIFY(clickAt(gp_Pnt(60, 40, 5)));
    QCOMPARE(_tool->sectionFacePickCount(), 1);
    QVERIFY(clickAt(gp_Pnt(30, 20, 5))); // elsewhere on the same cut face
    QCOMPARE(_tool->pickCount(), 0);
    QVERIFY(_results.empty());
    QVERIFY2(_viewport->context()->NbSelected() == 0, "the wall behind the cap stays unselected");
  }

  void wireframeDrawsNoCapsSoTheClickPicksWhatIsSeen() {
    sectionThrough(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1));
    _scene->setDisplayStyle(sc::render::DisplayStyle::Wireframe);
    render();
    _tool->setMode(MeasureMode::EntityInfo);
    QVERIFY(clickAt(gp_Pnt(60, 40, 5)));
    QCOMPARE(_results.size(), size_t(1));
    QVERIFY(_results.front().entities.front() != std::string("Section face on Fixture / Base plate"));
  }

  void movingAPlaneDropsAPendingSectionFace() {
    sectionThrough(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1));
    connect(_section.get(), &sc::render::SectionTool::planeChanged, _tool.get(),
            &sc::render::MeasureTool::dropSectionFacePicks);
    _tool->setMode(MeasureMode::Distance);
    QVERIFY(clickAt(gp_Pnt(60, 40, 5)));
    QCOMPARE(_tool->sectionFacePickCount(), 1);
    sc::render::SectionPlane plane = _section->plane(0);
    plane.offset = 3.0;
    _section->setPlane(0, plane);
    QCOMPARE(_tool->pickCount(), 0);
  }

  void withoutCapsTheClickPicksTheInside() {
    // No cap is drawn: the user sees into the plate, and the click picks its bottom face.
    sectionThrough(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1), false);
    _tool->setMode(MeasureMode::EntityInfo);
    QVERIFY(clickAt(gp_Pnt(60, 40, 5)));
    QCOMPARE(_results.size(), size_t(1));
    QCOMPARE(_results.front().entities.front(), std::string("Face (Plane) on Fixture / Base plate"));
  }

  void aClickThroughAHandleReachesThePart() {
    // A plane above the model cuts nothing, but its handle covers the view from the top.
    sectionThrough(gp_Pnt(0, 0, 35), gp_Dir(0, 0, 1));
    _tool->setMode(MeasureMode::Distance);
    QVERIFY(clickAt(gp_Pnt(60, 40, 10)));
    QCOMPARE(_tool->pickCount(), 1);
    QCOMPARE(_tool->sectionFacePickCount(), 0);
  }

  void theCutAwaySideCannotBePicked() {
    sectionThrough(gp_Pnt(50, 0, 0), gp_Dir(1, 0, 0)); // x > 50 is gone
    _tool->setMode(MeasureMode::Distance);
    clickAt(gp_Pnt(80, 40, 10));
    QCOMPARE(_tool->pickCount(), 0);
    QVERIFY(clickAt(gp_Pnt(20, 40, 10))); // the kept side still picks
    QCOMPARE(_tool->pickCount(), 1);
  }

  void movingAPlaneRefreshesTheCutFace() {
    sectionThrough(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1));
    _tool->setMode(MeasureMode::PointToPoint);
    hover(gp_Pnt(0.5, 0.5, 5));
    QVERIFY(_viewport->overlayMarker()->point.IsEqual(gp_Pnt(0, 0, 5), 1e-6));
    // The cached cut face was made for z = 5: after the move it must come from the new plane.
    sc::render::SectionPlane plane = _section->plane(0);
    plane.offset = 3.0;
    _section->setPlane(0, plane);
    render();
    hover(gp_Pnt(0.5, 0.5, 3));
    QVERIFY(_viewport->overlayMarker().has_value());
    QVERIFY(_viewport->overlayMarker()->point.IsEqual(gp_Pnt(0, 0, 3), 1e-6));
  }

  void pointToPointMeasuresBetweenCapCorners() {
    sectionThrough(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1));
    _tool->setMode(MeasureMode::PointToPoint);
    QVERIFY(clickAt(gp_Pnt(0.5, 0.5, 5)));
    QVERIFY(clickAt(gp_Pnt(99.5, 0.5, 5)));
    QCOMPARE(_results.size(), size_t(1));
    QVERIFY2(_results.front().ok, _results.front().error.c_str());
    QCOMPARE(_results.front().headline, std::string("100.000 mm"));
  }

  void leavingTheModeRemovesThePendingHighlight() {
    sectionThrough(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1));
    _tool->setMode(MeasureMode::Distance);
    const auto displayed = [this] {
      AIS_ListOfInteractive objects;
      _viewport->context()->DisplayedObjects(objects);
      return static_cast<int>(objects.Size());
    };
    const int before = displayed();
    QVERIFY(clickAt(gp_Pnt(60, 40, 5)));
    QCOMPARE(displayed(), before + 1);
    _tool->setMode(std::nullopt);
    QCOMPARE(displayed(), before);
    QCOMPARE(_tool->pickCount(), 0);
  }

  void pointToPointSnapsOnTheCap() {
    sectionThrough(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1));
    _tool->setMode(MeasureMode::PointToPoint);
    // Near the plate's corner: the cap's corner at z = 5, not the bottom face's at z = 0.
    hover(gp_Pnt(0.5, 0.5, 5));
    QVERIFY(_viewport->overlayMarker().has_value());
    QCOMPARE(_viewport->overlayMarker()->caption, QStringLiteral("Vertex"));
    QVERIFY(_viewport->overlayMarker()->point.IsEqual(gp_Pnt(0, 0, 5), 1e-6));
  }

  void turningOffRestoresWholePartSelection() {
    _tool->setMode(MeasureMode::EntityInfo);
    QVERIFY(clickAt(gp_Pnt(60, 40, 10)));
    QCOMPARE(_results.size(), size_t(1));
    QCOMPARE(_results.front().headline, std::string("6000.000 mm\xC2\xB2"));

    _tool->setWitnesses({{1, _results.front().witness, true}});
    _tool->setMode(std::nullopt);
    QVERIFY(_scene->selectionModes().empty());
    QVERIFY2(_tool->hasWitness(1), "stopping measuring keeps shown results on screen");
    QVERIFY(clickAt(gp_Pnt(60, 40, 10)));
    QCOMPARE(_scene->selectedParts().size(), size_t(1));
    QVERIFY2(_results.size() == 1, "no measurement when off");
  }
};

int sc::test::runMeasureToolTests(int argc, char** argv) {
  MeasureToolTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_measure_tool.moc"
