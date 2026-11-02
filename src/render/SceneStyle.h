#pragma once

#include "core/OccHandle.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>

namespace sc::render {

enum class DisplayStyle {
  Shaded,
  ShadedWithEdges,
  Wireframe,
};

// Applies the style to every displayed B-rep shape (helpers like the view cube are skipped).
void applyDisplayStyle(const OccHandle<AIS_InteractiveContext>& context, DisplayStyle style);

// Applies the style to one shape without redisplaying it.
void configureShapeStyle(const OccHandle<AIS_Shape>& shape, DisplayStyle style);

// Placeholder part shown until STEP import lands (M1): a plate with a boss and holes.
OccHandle<AIS_Shape> makeDemoPart();

} // namespace sc::render
