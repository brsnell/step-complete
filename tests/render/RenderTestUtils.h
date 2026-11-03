#pragma once

// Shared helpers for render tests.

#include <QImage>
#include <QRect>
#include <QString>

namespace sc::test {

inline double meanBrightness(const QImage& image, const QRect& area) {
  qint64 sum = 0;
  qint64 count = 0;
  for (int y = area.top(); y <= area.bottom(); ++y) {
    for (int x = area.left(); x <= area.right(); ++x) {
      sum += qGray(image.pixel(x, y));
      ++count;
    }
  }
  return count > 0 ? double(sum) / double(count) : 0.0;
}

inline double changedPixelFraction(const QImage& a, const QImage& b) {
  if (a.size() != b.size() || a.isNull()) {
    return 0.0;
  }
  qint64 changed = 0;
  for (int y = 0; y < a.height(); ++y) {
    for (int x = 0; x < a.width(); ++x) {
      const QRgb pa = a.pixel(x, y);
      const QRgb pb = b.pixel(x, y);
      const int diff =
          qAbs(qRed(pa) - qRed(pb)) + qAbs(qGreen(pa) - qGreen(pb)) + qAbs(qBlue(pa) - qBlue(pb));
      if (diff > 24) {
        ++changed;
      }
    }
  }
  return double(changed) / double(qint64(a.width()) * a.height());
}

// Set SC_RENDER_DUMP_DIR to keep the rendered frames for inspection.
inline void dumpImage(const QImage& image, const char* name) {
  const QString dir = qEnvironmentVariable("SC_RENDER_DUMP_DIR");
  if (!dir.isEmpty()) {
    image.save(dir + QLatin1Char('/') + QLatin1String(name) + QStringLiteral(".png"));
  }
}

// Run each render test class; defined in its own translation unit.
int runViewportSmokeTests(int argc, char** argv);
int runSceneManagerTests(int argc, char** argv);
int runMeasureToolTests(int argc, char** argv);

} // namespace sc::test
