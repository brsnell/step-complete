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
  std::unique_ptr<sc::render::OcctViewport> m_viewport;
  std::unique_ptr<sc::render::SceneManager> m_scene;
  std::unique_ptr<sc::render::MeasureTool> m_tool;
  std::vector<Measurement> m_results;

  QImage render() {
    m_viewport->invalidateScene();
    return m_viewport->grabFramebuffer();
  }

  // Hovers and clicks the model at a world point, like a user would.
  bool clickAt(const gp_Pnt& p) {
    const auto& view = m_viewport->view();
    const auto& context = m_viewport->context();
    int xp = 0;
    int yp = 0;
    view->Convert(p.X(), p.Y(), p.Z(), xp, yp);
    context->MoveTo(xp, yp, view, false);
    if (!context->HasDetected()) {
      return false;
    }
    context->SelectDetected(AIS_SelectionScheme_XOR);
    m_tool->onSelectionChanged();
    return true;
  }

  static std::string detail(const Measurement& m, const std::string& key) {
    for (const auto& [k, v] : m.details) {
      if (k == key) {
        return v;
      }
    }
    return {};
  }

private Q_SLOTS:
  void init() {
    m_viewport = std::make_unique<sc::render::OcctViewport>();
    m_viewport->resize(500, 400);
    m_viewport->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_viewport.get()));
    m_viewport->grabFramebuffer();
    QVERIFY(m_viewport->isGlReady());

    auto index = std::make_shared<sc::core::AssemblyIndex>(sc::test::buildFixtureAssembly(false));
    m_scene = std::make_unique<sc::render::SceneManager>(m_viewport->context());
    m_scene->setModel(index);
    m_tool = std::make_unique<sc::render::MeasureTool>(m_viewport.get(), m_scene.get());
    m_results.clear();
    connect(m_tool.get(), &sc::render::MeasureTool::measured, this,
            [this](const Measurement& m) { m_results.push_back(m); });
    m_viewport->setStandardView(V3d_Zpos);
    m_viewport->fitAll();
    render();
  }

  void cleanup() {
    m_tool.reset();
    m_scene.reset();
    m_viewport.reset();
  }

  void distanceBetweenFacesOfDifferentInstances() {
    m_tool->setMode(MeasureMode::Distance);
    // Top face of the base plate (z = 10), away from the pins, then the top face of
    // the first pin (pin pair at (10,10,10), pin height 20: top at z = 30).
    QVERIFY(clickAt(gp_Pnt(60, 40, 10)));
    QCOMPARE(m_tool->pickCount(), 1);
    QVERIFY(m_results.empty());
    QVERIFY(clickAt(gp_Pnt(10, 10, 30)));

    QCOMPARE(m_results.size(), size_t(1));
    const Measurement& m = m_results.front();
    QVERIFY2(m.ok, m.error.c_str());
    QCOMPARE(m.headline, std::string("20.000 mm"));
    QCOMPARE(detail(m, "Parallel distance"), std::string("20.000 mm"));
    QCOMPARE(m.entities.size(), size_t(2));
    QCOMPARE(m.entities[0], std::string("Face (Plane) on Fixture / Base plate"));
    QCOMPARE(m_tool->pickCount(), 0);
    QVERIFY(m_tool->hasWitness());
  }

  void radiusOfAPlacedInstance() {
    // The second pin of the second pair sits at (50,10,10) + (20,0,0): only correct
    // if the instance placement is applied to the picked prototype edge.
    m_tool->setMode(MeasureMode::Radius);
    QVERIFY(clickAt(gp_Pnt(73, 10, 30))); // on the top circle, radius 3 around (70,10)
    QCOMPARE(m_results.size(), size_t(1));
    const Measurement& m = m_results.front();
    QVERIFY2(m.ok, m.error.c_str());
    QCOMPARE(detail(m, "Radius"), std::string("3.000 mm"));
    QCOMPARE(detail(m, "Center"), std::string("70.000, 10.000, 30.000 mm"));
  }

  void clickingAPickAgainUnpicksIt() {
    m_tool->setMode(MeasureMode::Angle);
    QVERIFY(clickAt(gp_Pnt(60, 40, 10)));
    QCOMPARE(m_tool->pickCount(), 1);
    QVERIFY(clickAt(gp_Pnt(60, 40, 10)));
    QCOMPARE(m_tool->pickCount(), 0);
    QVERIFY(m_results.empty());
  }

  void witnessIsDrawnOnTop() {
    const QImage before = render();
    m_tool->setMode(MeasureMode::Distance);
    QVERIFY(clickAt(gp_Pnt(60, 40, 10)));
    QVERIFY(clickAt(gp_Pnt(10, 10, 30)));
    QCOMPARE(m_results.size(), size_t(1));
    const QImage after = render();
    sc::test::dumpImage(after, "measure_witness");

    int orange = 0;
    for (int y = 0; y < after.height(); ++y) {
      for (int x = 0; x < after.width(); ++x) {
        const QColor c = after.pixelColor(x, y);
        if (c.red() > 200 && c.green() > 70 && c.green() < 170 && c.blue() < 80) {
          ++orange;
        }
      }
    }
    QVERIFY2(orange > 10, "witness geometry is not visible");
    QVERIFY(sc::test::changedPixelFraction(before, after) > 0.0005);

    m_tool->clearWitness();
    QVERIFY2(sc::test::changedPixelFraction(before, render()) < 0.0005, "witness was not removed");
  }

  void turningOffRestoresWholePartSelection() {
    m_tool->setMode(MeasureMode::EntityInfo);
    QVERIFY(clickAt(gp_Pnt(60, 40, 10)));
    QCOMPARE(m_results.size(), size_t(1));
    QCOMPARE(m_results.front().headline, std::string("6000.000 mm\xC2\xB2"));

    m_tool->setMode(std::nullopt);
    QVERIFY(m_scene->selectionModes().empty());
    QVERIFY(!m_tool->hasWitness());
    QVERIFY(clickAt(gp_Pnt(60, 40, 10)));
    QCOMPARE(m_scene->selectedParts().size(), size_t(1));
    QVERIFY2(m_results.size() == 1, "no measurement when off");
  }
};

int sc::test::runMeasureToolTests(int argc, char** argv) {
  MeasureToolTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_measure_tool.moc"
