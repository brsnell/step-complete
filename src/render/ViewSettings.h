#pragma once

#include <QColor>

namespace sc::render {

// How the 3D view's background is drawn.
struct BackgroundStyle {
  enum class Kind { Gradient, Solid, White };
  Kind kind = Kind::Gradient;
  QColor top = QColor(230, 235, 242);    // gradient: top color
  QColor bottom = QColor(158, 168, 184); // gradient: bottom color
  QColor solid = QColor(230, 235, 242);  // solid: the color

  bool operator==(const BackgroundStyle& other) const {
    return kind == other.kind && top == other.top && bottom == other.bottom && solid == other.solid;
  }
  bool operator!=(const BackgroundStyle& other) const { return !(*this == other); }
};

// Light setups. All use one headlight plus ambient light (cheap: no shadows).
enum class LightingPreset { Default, Brighter, Softer };

// Background used when exporting an image.
enum class ExportBackground { AsShown, White, Transparent };

} // namespace sc::render
