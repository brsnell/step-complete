// Image export (OcctViewport::renderImage) and view preferences (background, lighting).
// Needs a display with OpenGL; in CI this runs under Xvfb with Mesa llvmpipe.

#include "render/OcctViewport.h"
#include "render/SceneStyle.h"

#include <QImage>
#include <QtTest>

#include "RenderTestUtils.h"

using sc::render::BackgroundStyle;
using sc::render::ExportBackground;
using sc::render::LightingPreset;
using sc::render::OcctViewport;
using sc::test::dumpImage;
using sc::test::meanBrightness;

namespace {

bool colorsClose(QRgb a, QRgb b, int tolerance) {
  return qAbs(qRed(a) - qRed(b)) <= tolerance && qAbs(qGreen(a) - qGreen(b)) <= tolerance &&
         qAbs(qBlue(a) - qBlue(b)) <= tolerance;
}

// Fraction of pixels that differ by more than 12 in some channel.
double differingFraction(const QImage& a, const QImage& b) {
  qint64 differing = 0;
  qint64 total = 0;
  for (int y = 0; y < a.height(); ++y) {
    for (int x = 0; x < a.width(); ++x) {
      ++total;
      differing += colorsClose(a.pixel(x, y), b.pixel(x, y), 12) ? 0 : 1;
    }
  }
  return total > 0 ? double(differing) / double(total) : 1.0;
}

bool cornersAre(const QImage& image, QRgb expected, int tolerance) {
  const int w = image.width() - 1;
  const int h = image.height() - 1;
  return colorsClose(image.pixel(0, 0), expected, tolerance) &&
         colorsClose(image.pixel(w, 0), expected, tolerance) &&
         colorsClose(image.pixel(0, h), expected, tolerance) &&
         colorsClose(image.pixel(w, h), expected, tolerance);
}

BackgroundStyle whiteStyle() {
  BackgroundStyle style;
  style.kind = BackgroundStyle::Kind::White;
  return style;
}

} // namespace

class ViewSettingsTest : public QObject {
  Q_OBJECT

private:
  // Shows the viewport with the demo part fitted.
  void prepare(OcctViewport& viewport) {
    viewport.resize(320, 240);
    viewport.show();
    QVERIFY(QTest::qWaitForWindowExposed(&viewport));
    viewport.grabFramebuffer();
    QVERIFY(viewport.isGlReady());
    viewport.context()->Display(sc::render::makeDemoPart(), false);
    viewport.fitAll();
  }

private Q_SLOTS:
  void exportAtViewSizeMatchesGrab() {
    OcctViewport viewport;
    prepare(viewport);
    const QSize size = viewport.framebufferSize();
    const QImage grab = viewport.grabFramebuffer().convertToFormat(QImage::Format_RGB32);
    const QImage exported = viewport.renderImage(size.width(), size.height(), ExportBackground::AsShown);
    QCOMPARE(exported.size(), grab.size());
    QVERIFY(differingFraction(grab, exported) < 0.001);

    // The offscreen path (any non-"as shown" mode) must agree with the screen as well:
    // compare a white export with a grab taken on a white background.
    viewport.setBackground(whiteStyle());
    const QImage whiteGrab = viewport.grabFramebuffer().convertToFormat(QImage::Format_RGB32);
    const QImage whiteDump = viewport.renderImage(size.width(), size.height(), ExportBackground::White);
    dumpImage(whiteGrab, "export_white_grab");
    dumpImage(whiteDump, "export_white_dump");
    QCOMPARE(whiteDump.size(), whiteGrab.size());
    QVERIFY2(differingFraction(whiteGrab, whiteDump) < 0.02, "offscreen dump differs from the screen");
  }

  void exportAtLargerSize() {
    OcctViewport viewport;
    prepare(viewport);
    const QImage image = viewport.renderImage(1200, 900, ExportBackground::AsShown);
    dumpImage(image, "export_1200x900");
    QCOMPARE(image.size(), QSize(1200, 900));

    // Non-background pixels near the center (the model).
    int modelPixels = 0;
    for (int y = 400; y < 500; ++y) {
      for (int x = 550; x < 650; ++x) {
        modelPixels += colorsClose(image.pixel(x, y), image.pixel(x, 2), 20) ? 0 : 1;
      }
    }
    QVERIFY2(modelPixels > 3000, "model is not visible in the large export");
    QVERIFY(!colorsClose(image.pixel(2, 2), image.pixel(2, 897), 10)); // gradient preserved

    // A very different aspect ratio.
    const QImage wide = viewport.renderImage(900, 200, ExportBackground::AsShown);
    dumpImage(wide, "export_900x200");
    QCOMPARE(wide.size(), QSize(900, 200));
  }

  void whiteAndTransparentBackground() {
    OcctViewport viewport;
    prepare(viewport);
    const QImage white = viewport.renderImage(640, 480, ExportBackground::White);
    dumpImage(white, "export_white");
    QVERIFY(cornersAre(white, qRgb(255, 255, 255), 1));
    QVERIFY(!colorsClose(white.pixel(320, 240), qRgb(255, 255, 255), 30));

    const QImage transparent = viewport.renderImage(640, 480, ExportBackground::Transparent);
    dumpImage(transparent, "export_transparent");
    QVERIFY(transparent.hasAlphaChannel());
    QCOMPARE(qAlpha(transparent.pixel(1, 1)), 0);
    QCOMPARE(qAlpha(transparent.pixel(638, 478)), 0);
    QVERIFY(qAlpha(transparent.pixel(320, 240)) > 250);
    // Opaque model pixels equal the white-background render.
    QVERIFY(colorsClose(transparent.pixel(320, 240) | 0xff000000, white.pixel(320, 240), 3));
  }

  void exportLeavesScreenUnchanged() {
    OcctViewport viewport;
    prepare(viewport);
    const QImage before = viewport.grabFramebuffer();
    viewport.renderImage(1000, 300, ExportBackground::White);
    viewport.renderImage(500, 500, ExportBackground::Transparent);
    viewport.renderImage(640, 480, ExportBackground::AsShown);
    const QImage after = viewport.grabFramebuffer();
    QCOMPARE(after.size(), before.size());
    QVERIFY2(differingFraction(before, after) < 0.0005, "on-screen frame changed after export");
    QVERIFY(qAbs(viewport.view()->Camera()->Aspect() - double(before.width()) / before.height()) < 1e-3);
  }

  void backgroundStyles() {
    OcctViewport viewport;
    prepare(viewport);
    const QImage gradient = viewport.grabFramebuffer();
    QVERIFY(!cornersAre(gradient, qRgb(255, 255, 255), 10));

    viewport.setBackground(whiteStyle());
    const QImage white = viewport.grabFramebuffer();
    QVERIFY(cornersAre(white, qRgb(255, 255, 255), 1));

    BackgroundStyle solid;
    solid.kind = BackgroundStyle::Kind::Solid;
    solid.solid = QColor(200, 30, 30);
    viewport.setBackground(solid);
    const QImage solidImage = viewport.grabFramebuffer();
    QVERIFY(cornersAre(solidImage, qRgb(200, 30, 30), 8));

    BackgroundStyle custom;
    custom.top = QColor(0, 0, 255);
    custom.bottom = QColor(0, 255, 0);
    viewport.setBackground(custom);
    const QImage twoTone = viewport.grabFramebuffer();
    QVERIFY(qBlue(twoTone.pixel(2, 2)) > 200 && qGreen(twoTone.pixel(2, twoTone.height() - 3)) > 200);
  }

  void lightingChangesShading() {
    OcctViewport viewport;
    prepare(viewport);
    sc::render::applyDisplayStyle(viewport.context(), sc::render::DisplayStyle::Shaded);
    viewport.setBackground(whiteStyle());
    viewport.invalidateScene();

    auto brightness = [&viewport](LightingPreset preset) {
      viewport.setLighting(preset);
      const QImage image = viewport.grabFramebuffer();
      dumpImage(image, preset == LightingPreset::Default    ? "lighting_default"
                       : preset == LightingPreset::Brighter ? "lighting_brighter"
                                                            : "lighting_softer");
      const QRect center(image.width() / 2 - 40, image.height() / 2 - 30, 80, 60);
      return meanBrightness(image, center);
    };
    const double normal = brightness(LightingPreset::Default);
    const double bright = brightness(LightingPreset::Brighter);
    const double soft = brightness(LightingPreset::Softer);
    qInfo("shaded brightness: default %.1f, brighter %.1f, softer %.1f", normal, bright, soft);
    QVERIFY(bright > normal + 5);
    QVERIFY(soft < normal - 5);
    QCOMPARE(viewport.lighting(), LightingPreset::Softer);
  }
};

int sc::test::runViewSettingsTests(int argc, char** argv) {
  ViewSettingsTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_view_settings.moc"
