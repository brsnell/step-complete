// Render smoke test: proves the Qt <-> OCCT OpenGL bridge works end to end.
// Renders the background alone, then with the demo part, and checks that a
// meaningful share of pixels changed. Also exercises a resize.
//
// Needs a display with OpenGL; in CI this runs under Xvfb with Mesa llvmpipe.

#include "render/OcctViewport.h"
#include "render/SceneStyle.h"

#include <QApplication>
#include <QImage>
#include <QMainWindow>
#include <QMenuBar>
#include <QScreen>
#include <QStatusBar>
#include <QtTest>

#include "RenderTestUtils.h"

using sc::test::changedPixelFraction;
using sc::test::dumpImage;
using sc::test::meanBrightness;

class ViewportSmokeTest : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void rendersDemoPart() {
    sc::render::OcctViewport viewport;
    viewport.resize(320, 240);
    viewport.show();
    QVERIFY(QTest::qWaitForWindowExposed(&viewport));

    const QImage background = viewport.grabFramebuffer();
    QVERIFY2(viewport.isGlReady(), "OCCT failed to wrap Qt's OpenGL context");
    QVERIFY(!background.isNull());
    qInfo("%s", qPrintable(viewport.glSummary()));

    viewport.context()->Display(sc::render::makeDemoPart(), false);
    viewport.fitAll();
    const QImage withPart = viewport.grabFramebuffer();

    const double fraction = changedPixelFraction(background, withPart);
    qInfo("changed pixels: %.1f%%", fraction * 100.0);
    QVERIFY2(fraction > 0.10, "demo part does not appear to be rendered");

    // Resizing must resize OCCT's view to match Qt's new FBO.
    viewport.resize(480, 300);
    QTest::qWait(50);
    const QImage resized = viewport.grabFramebuffer();
    const qreal dpr = viewport.devicePixelRatioF();
    QCOMPARE(resized.width(), qRound(480 * dpr));
    QCOMPARE(resized.height(), qRound(300 * dpr));
  }

  void switchesDisplayStyles() {
    sc::render::OcctViewport viewport;
    viewport.resize(320, 240);
    viewport.show();
    QVERIFY(QTest::qWaitForWindowExposed(&viewport));
    viewport.grabFramebuffer();
    QVERIFY(viewport.isGlReady());

    viewport.context()->Display(sc::render::makeDemoPart(), false);
    viewport.fitAll();

    auto renderStyle = [&viewport](sc::render::DisplayStyle style, const char* name) {
      sc::render::applyDisplayStyle(viewport.context(), style);
      viewport.invalidateScene();
      const QImage image = viewport.grabFramebuffer();
      dumpImage(image, name);
      return image;
    };
    const QImage edges = renderStyle(sc::render::DisplayStyle::ShadedWithEdges, "shaded_edges");
    const QImage shaded = renderStyle(sc::render::DisplayStyle::Shaded, "shaded");
    const QImage wireframe = renderStyle(sc::render::DisplayStyle::Wireframe, "wireframe");

    const double edgeDiff = changedPixelFraction(shaded, edges);
    const double wireDiff = changedPixelFraction(shaded, wireframe);
    qInfo("edges vs shaded: %.1f%%, wireframe vs shaded: %.1f%%", edgeDiff * 100.0, wireDiff * 100.0);
    QVERIFY2(edgeDiff > 0.005, "face boundary edges are not drawn");
    QVERIFY2(wireDiff > 0.05, "wireframe looks identical to shaded");
  }
  // Regression: giving OCCT the viewport's own native window (winId()) made the
  // widget a native child and broke Qt's compositing of the other widgets: the
  // menu and status bars rendered black. Only a real screen grab shows this;
  // QWidget::grab() composes offscreen and would hide the bug.
  void composesInsideMainWindow() {
    QMainWindow window;
    window.menuBar()->addMenu(QStringLiteral("File"));
    window.statusBar()->showMessage(QStringLiteral("status"));
    auto* viewport = new sc::render::OcctViewport(&window);
    window.setCentralWidget(viewport);
    window.resize(480, 360);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY_WITH_TIMEOUT(viewport->isGlReady(), 5000);
    viewport->context()->Display(sc::render::makeDemoPart(), false);
    viewport->fitAll();
    QTest::qWait(300);

    QScreen* screen = window.screen();
    QVERIFY(screen != nullptr);
    const QImage shot = screen->grabWindow(window.winId()).toImage();
    QVERIFY(!shot.isNull());
    dumpImage(shot, "main_window");

    const qreal dpr = shot.devicePixelRatio();
    auto toImageRect = [dpr](const QRect& r) {
      return QRect(qRound(r.x() * dpr), qRound(r.y() * dpr), qRound(r.width() * dpr),
                   qRound(r.height() * dpr));
    };
    const QRect menuArea = toImageRect(window.menuBar()->geometry()).intersected(shot.rect());
    const QRect statusArea = toImageRect(window.statusBar()->geometry()).intersected(shot.rect());
    const QRect viewArea = toImageRect(viewport->geometry()).intersected(shot.rect());
    qInfo("brightness: menu %.0f, status %.0f, viewport %.0f", meanBrightness(shot, menuArea),
          meanBrightness(shot, statusArea), meanBrightness(shot, viewArea));
    // A grab that is black everywhere, viewport included, means the screen cannot be
    // read back (XWayland under a Wayland compositor), not the regression, which
    // leaves the viewport drawn. Run under Xvfb to exercise this test.
    if (meanBrightness(shot, shot.rect()) < 1.0) {
      QSKIP("screen grab is blank (Wayland session?); run under xvfb-run to test composition");
    }
    QVERIFY2(meanBrightness(shot, menuArea) > 60, "menu bar is black: window composition is broken");
    QVERIFY2(meanBrightness(shot, statusArea) > 60, "status bar is black: window composition is broken");
    QVERIFY2(meanBrightness(shot, viewArea) > 60, "viewport is black");
  }
};

int sc::test::runViewportSmokeTests(int argc, char** argv) {
  ViewportSmokeTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_viewport_smoke.moc"
