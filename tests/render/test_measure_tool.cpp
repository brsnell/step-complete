// MeasureTool tests: real picks through OCCT's selector on the fixture assembly,
// in a live viewport, down to the measured values and the drawn witness.

#include "RenderTestUtils.h"
#include "TestModels.h"

#include "core/AssemblyIndex.h"
#include "render/MeasureTool.h"
#include "render/OcctViewport.h"
#include "render/SceneManager.h"

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
  std::vector<Measurement> _results;

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
