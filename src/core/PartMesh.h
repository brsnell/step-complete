#pragma once

#include "core/OccHandle.h"

#include <Poly_Triangulation.hxx>
#include <TopoDS_Shape.hxx>

namespace sc::core {

// Splitting large triangles, so a big flat face gets points inside it (a box face otherwise has
// only its four corners): triangles with an edge longer than longestFraction of the mesh's size
// (its box diagonal) are split into four, level by level, up to 'levels' times and 'maxPoints'
// points. Midpoints lie on the straight edges, so neighbours that split differently leave no gap.
struct MeshRefinement {
  double longestFraction = 0.0; // 0: no refinement
  int levels = 0;
  int maxPoints = 300000;
};

// Every face triangulation of a shape merged into one mesh, in the shape's own coordinates (its
// location included), triangles wound outward (reversed faces swap two corners) and each node
// given the area-weighted average normal of its face's triangles. Faces keep their own nodes, so
// sharp edges keep sharp normals. Faces without a triangulation are left out; null when none has
// one, refined when asked. Used by wall thickness and model comparison.
OccHandle<Poly_Triangulation> partMesh(const TopoDS_Shape& shape, const MeshRefinement& refinement = {});

} // namespace sc::core
