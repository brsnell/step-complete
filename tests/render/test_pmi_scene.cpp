// PmiScene tests: PMI drawn on every instance of its part, visibility, picking,
// cross-highlighting of target geometry, and saved views.

#include "PmiFixture.h"
#include "RenderTestUtils.h"

#include "core/AssemblyIndex.h"
#include "core/PmiModel.h"
#include "render/OcctViewport.h"
#include "render/PmiScene.h"
#include "render/SceneManager.h"

#include <TDataStd_Name.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Trsf.hxx>

#include <QtTest>

#include <memory>

using sc::core::PmiId;
using sc::core::PmiModel;

namespace {

int countPixels(const QImage& image, bool (*match)(const QColor&)) {
  int count = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      count += match(image.pixelColor(x, y)) ? 1 : 0;
    }
  }
  return count;
}

bool isPmiBlue(const QColor& c) {
  return c.blue() > 90 && c.blue() > c.red() + 50 && c.blue() > c.green() + 30 && c.red() < 80;
}

bool isTargetOrange(const QColor& c) {
  return c.red() > 200 && c.green() > 100 && c.green() < 200 && c.blue() < 90;
}

} // namespace

class PmiSceneTest : public QObject {
  Q_OBJECT

private:
  std::unique_ptr<sc::render::OcctViewport> m_viewport;
  std::unique_ptr<sc::render::SceneManager> m_scene;
  std::unique_ptr<sc::render::PmiScene> m_pmiScene;
  std::shared_ptr<PmiModel> m_pmi;
  sc::test::PmiFixture m_fixture;

  QImage render() {
    m_viewport->invalidateScene();
    return m_viewport->grabFramebuffer();
  }

  // Puts the PMI part into an assembly twice: at the origin and 150 mm along X.
  void makeTwoInstances() {
    const TDF_Label main = m_fixture.document->ocaf()->Main();
    auto shapes = XCAFDoc_DocumentTool::ShapeTool(main);
    const TDF_Label assembly = shapes->NewShape();
    TDataStd_Name::Set(assembly, "Pair");
    shapes->AddComponent(assembly, m_fixture.part, TopLoc_Location());
    gp_Trsf shift;
    shift.SetTranslation(gp_Vec(150, 0, 0));
    shapes->AddComponent(assembly, m_fixture.part, TopLoc_Location(shift));
    shapes->UpdateAssemblies();
  }

  void load() {
    auto index = std::make_shared<sc::core::AssemblyIndex>(m_fixture.document);
    m_scene->setModel(index);
    m_pmi = std::make_shared<PmiModel>(m_fixture.document);
    m_pmiScene->setModel(m_pmi, index);
    m_viewport->setStandardView(V3d_Zpos);
    m_viewport->fitAll();
  }

  PmiId id(const TDF_Label& label) const { return m_pmi->itemFor(label); }

private Q_SLOTS:
  void init() {
    m_viewport = std::make_unique<sc::render::OcctViewport>();
    m_viewport->resize(500, 400);
    m_viewport->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_viewport.get()));
    m_viewport->grabFramebuffer();
    QVERIFY(m_viewport->isGlReady());
    m_fixture = sc::test::buildPmiFixture();
    m_scene = std::make_unique<sc::render::SceneManager>(m_viewport->context());
    m_pmiScene = std::make_unique<sc::render::PmiScene>(m_viewport->context());
  }

  void cleanup() {
    m_pmiScene.reset();
    m_scene.reset();
    m_viewport.reset();
  }

  void displaysEveryItemOnEveryInstance() {
    makeTwoInstances();
    load();
    QCOMPARE(m_pmiScene->presentationCount(), static_cast<int>(m_pmi->items().size()) * 2);
    QCOMPARE(m_pmiScene->visibleIds().size(), m_pmi->items().size());
  }

  void drawsGraphicalAndSemanticPmiAndHidesIt() {
    load();
    const QImage shown = render();
    sc::test::dumpImage(shown, "pmi_shown");
    m_pmiScene->setVisible(m_pmiScene->visibleIds(), false);
    QVERIFY(m_pmiScene->visibleIds().empty());
    const QImage hidden = render();
    QVERIFY2(countPixels(shown, isPmiBlue) > countPixels(hidden, isPmiBlue) + 50,
             "PMI (presentation lines and labels) is not drawn");
    QVERIFY(sc::test::changedPixelFraction(shown, hidden) > 0.002);

    m_pmiScene->setVisible({id(m_fixture.flatness)}, true);
    QCOMPARE(m_pmiScene->visibleIds(), std::vector<PmiId>{id(m_fixture.flatness)});
    QVERIFY(m_pmiScene->isVisible(id(m_fixture.flatness)));
    QVERIFY(!m_pmiScene->isVisible(id(m_fixture.height)));
  }

  void highlightsTheGeometryAnAnnotationAppliesTo() {
    load();
    const QImage before = render();
    QCOMPARE(countPixels(before, isTargetOrange), 0);
    m_pmiScene->highlight({id(m_fixture.flatness)}); // the top face
    const QImage after = render();
    sc::test::dumpImage(after, "pmi_target_highlight");
    QVERIFY2(countPixels(after, isTargetOrange) > after.width() * after.height() / 10,
             "the top face is not highlighted");
    QCOMPARE(m_pmiScene->selectedPmi(), std::vector<PmiId>{id(m_fixture.flatness)});

    m_pmiScene->highlight({});
    QCOMPARE(countPixels(render(), isTargetOrange), 0);
  }

  void pickingALabelSelectsItsPmi() {
    load();
    // Show only the flatness: its label sits on the top face's center (no text position in the file).
    m_pmiScene->setOnlyVisible({id(m_fixture.flatness)});
    render();
    int xp = 0;
    int yp = 0;
    m_viewport->view()->Convert(50, 30, 20, xp, yp);
    const auto& context = m_viewport->context();
    context->MoveTo(xp, yp, m_viewport->view(), false);
    QVERIFY(context->HasDetected());
    QCOMPARE(m_pmiScene->pmiOf(context->DetectedInteractive()), id(m_fixture.flatness));
    QVERIFY(m_scene->partOf(context->DetectedInteractive()) == sc::core::kNoNode);
  }

  void appliesSavedViews() {
    load();
    QCOMPARE(m_pmi->views().size(), size_t(1));
    m_pmiScene->applyView(m_pmi->views().front(), m_viewport->view());
    std::vector<PmiId> expected{id(m_fixture.flatness), id(m_fixture.height)};
    std::sort(expected.begin(), expected.end());
    QCOMPARE(m_pmiScene->visibleIds(), expected);
    const auto& camera = m_viewport->view()->Camera();
    QVERIFY(camera->Direction().IsEqual(gp_Dir(0, 1, 0), 1e-9));
    QVERIFY(camera->Up().IsEqual(gp_Dir(0, 0, 1), 1e-9));
    QVERIFY(camera->IsOrthographic());
    QVERIFY(std::abs(camera->Scale() - 80.0) < 1e-6);
    QCOMPARE(m_viewport->view()->ClipPlanes()->Size(), 1);
    sc::test::dumpImage(render(), "pmi_saved_view");

    m_pmiScene->clearViewClipping(m_viewport->view());
    QVERIFY(m_viewport->view()->ClipPlanes().IsNull() || m_viewport->view()->ClipPlanes()->Size() == 0);
  }
};

int sc::test::runPmiSceneTests(int argc, char** argv) {
  PmiSceneTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_pmi_scene.moc"
