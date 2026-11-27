#pragma once

// Section caps for measuring (docs/designs/section-aware-measurement.md): which cap a click
// landed on, and the cap's face, from the section planes' geometry alone. OCCT only.

#include "core/Measurement.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

#include <optional>
#include <vector>

namespace sc::core {

// A section plane: its normal points to the side that is cut away.
struct CutPlane {
  gp_Pln plane;
  bool capped = true; // the cut solids are filled on this plane
};

struct CapHit {
  size_t plane = 0; // index into the planes
  gp_Pnt point;     // where the click crossed the cap
};

// A click hit 'hitPoint' on 'part' (world coordinates), looking along 'viewDirection' (from the
// eye into the scene). Returns the cap the click crossed before reaching the hit: the plane
// where the ray last entered the kept side of all planes, when that plane is capped and the
// crossing is inside one of the part's solids. nullopt when the hit is what the user sees.
std::optional<CapHit> capHit(const TopoDS_Shape& part, const gp_Pnt& hitPoint, const gp_Dir& viewDirection,
                             const std::vector<CutPlane>& planes);

// The cap faces of plane 'index' on 'part': the part's solids in common with the plane, minus
// what the other planes cut away. A compound of faces, null when the plane misses the solids.
TopoDS_Shape capFaces(const TopoDS_Shape& part, const std::vector<CutPlane>& planes, size_t index);

// The face of 'faces' that 'point' is on (within a small tolerance), or nullopt.
std::optional<TopoDS_Face> faceAt(const TopoDS_Shape& faces, const gp_Pnt& point);

// Point to Point snapping on a cap face, which has no vertex or edge picks of its own: its
// corners, then its edges' endpoints, midpoints and circle centers, within 'snapDistance';
// then the closest point on an edge within it; otherwise the point itself, on the face.
SnapResult snapOnCap(const TopoDS_Face& face, const gp_Pnt& picked, double snapDistance);

} // namespace sc::core
