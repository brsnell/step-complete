#pragma once

#include "core/OccHandle.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <Prs3d_Drawer.hxx>

namespace sc::render {

enum class DisplayStyle {
  Shaded,
  ShadedWithEdges,
  Wireframe,
  // Faces in flat white, sharp edges and silhouettes in dark lines; edges behind faces are hidden.
  HiddenLine,
};

// True for the styles that draw face boundary edges over the shaded faces.
inline bool drawsFaceEdges(DisplayStyle style) {
  return style == DisplayStyle::ShadedWithEdges || style == DisplayStyle::HiddenLine;
}

// Sets the drawer's own shading aspect for the hidden-line look (on) or back to lit shading (off).
// Turning it off leaves the face color as hidden-line set it; callers restore their colors.
void configureHiddenLineFaces(const OccHandle<Prs3d_Drawer>& drawer, bool on);

// Applies the style to every displayed B-rep shape (helpers like the view cube are skipped).
void applyDisplayStyle(const OccHandle<AIS_InteractiveContext>& context, DisplayStyle style);

// Applies the style to one shape without redisplaying it.
void configureShapeStyle(const OccHandle<AIS_Shape>& shape, DisplayStyle style);

// Placeholder part shown until STEP import lands (M1): a plate with a boss and holes.
OccHandle<AIS_Shape> makeDemoPart();

} // namespace sc::render
