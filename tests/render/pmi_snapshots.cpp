// Renders every NIST PMI test model (isometric view plus each saved view) to PNG
// files in SC_RENDER_DUMP_DIR. CI runs it with OCCT 8 and uploads the images, so
// what users would see can be checked without a local OCCT 8 build.
//
// Also a smoke test: each model must load, show PMI and render something.
// Skips when SC_TEST_DATA_DIR is not set (run tests/data/fetch.sh first).

#include "RenderTestUtils.h"

#include "core/AssemblyIndex.h"
#include "core/PmiModel.h"
#include "core/StepImporter.h"
#include "render/OcctViewport.h"
#include "render/PmiScene.h"
#include "render/SceneManager.h"

#include <Standard_Version.hxx>

#include <QApplication>
#include <QDir>
#include <QtTest>

#include <memory>

namespace {

const char* const kModels[] = {
    "nist_ctc_01_asme1_ap242-e1.stp", "nist_ctc_02_asme1_ap242-e2.stp", "nist_ctc_03_asme1_ap242-e2.stp",
    "nist_ctc_04_asme1_ap242-e1.stp", "nist_ctc_05_asme1_ap242-e1.stp", "nist_ftc_06_asme1_ap242-e2.stp",
    "nist_ftc_07_asme1_ap242-e2.stp", "nist_ftc_10_asme1_ap242-e2.stp",
};

QString safeName(QString name) {
  for (QChar& c : name) {
    if (!c.isLetterOrNumber()) {
      c = QLatin1Char('_');
    }
  }
  return name;
}

} // namespace

class PmiSnapshots : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void renderModels_data() {
    QTest::addColumn<QString>("file");
    for (const char* model : kModels) {
      QTest::newRow(model) << QString::fromLatin1(model);
    }
  }

  void renderModels() {
    QFETCH(QString, file);
    const QString dataDir = qEnvironmentVariable("SC_TEST_DATA_DIR");
    const QString path = QDir(dataDir).filePath(file);
    if (dataDir.isEmpty() || !QFileInfo::exists(path)) {
      // (Not QSKIP: its one-argument form trips -Wpedantic with Qt 6.8 on clang.)
      QTest::qSkip("NIST model not available (run tests/data/fetch.sh and set SC_TEST_DATA_DIR)", __FILE__,
                   __LINE__);
      return;
    }

    const sc::core::ImportResult result = sc::core::importStep(path.toStdString());
    QVERIFY2(result.ok, result.error.c_str());
    auto index = std::make_shared<sc::core::AssemblyIndex>(result.document);
    auto pmi = std::make_shared<sc::core::PmiModel>(result.document);

    sc::render::OcctViewport viewport;
    viewport.resize(1280, 900);
    viewport.show();
    QVERIFY(QTest::qWaitForWindowExposed(&viewport));
    viewport.grabFramebuffer();
    QVERIFY(viewport.isGlReady());

    sc::render::SceneManager scene(viewport.context());
    sc::render::PmiScene pmiScene(viewport.context());
    scene.setModel(index);
    pmiScene.setModel(pmi, index, viewport.devicePixelRatioF());
    QVERIFY(pmiScene.presentationCount() > 0);

    const QString base = safeName(QFileInfo(file).completeBaseName()) +
                         QStringLiteral("_occt%1").arg(QLatin1String(OCC_VERSION_STRING_EXT));
    const auto snap = [&](const QString& suffix) {
      viewport.invalidateScene();
      // Composited grab: includes the PMI labels' overlay widgets, if any.
      const QImage image = viewport.grab().toImage();
      sc::test::dumpImage(image, qPrintable(base + QLatin1Char('_') + safeName(suffix)));
      return image;
    };

    viewport.setStandardView(V3d_XposYnegZpos);
    viewport.fitAll();
    const QImage iso = snap(QStringLiteral("iso"));
    QVERIFY2(sc::test::meanBrightness(iso, iso.rect()) > 0.0, "nothing rendered");

    for (const sc::core::PmiSavedView& view : pmi->views()) {
      pmiScene.applyView(view, viewport.view());
      snap(QStringLiteral("view_") + QString::fromStdString(view.name));
      pmiScene.clearViewClipping(viewport.view());
    }
    qInfo("%s: %d annotations, %d saved views", qPrintable(file), static_cast<int>(pmi->items().size()),
          static_cast<int>(pmi->views().size()));
  }
};

int main(int argc, char** argv) {
#if defined(__linux__)
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "xcb");
  }
  if (qEnvironmentVariableIsEmpty("QT_XCB_GL_INTEGRATION")) {
    qputenv("QT_XCB_GL_INTEGRATION", "xcb_glx");
  }
#endif
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  sc::render::OcctViewport::configureDefaultSurfaceFormat();
  QApplication app(argc, argv);
  PmiSnapshots test;
  return QTest::qExec(&test, argc, argv);
}

#include "pmi_snapshots.moc"
