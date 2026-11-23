#pragma once

#include "core/OccHandle.h"

#include <Poly_Triangulation.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <array>
#include <optional>
#include <vector>

namespace sc::core {

// Closest-point queries against a triangle mesh (model comparison, docs/designs/model-comparison.md).
// Built once from a mesh whose triangles are wound outward (core/PartMesh), placed by a
// transformation; queries are read-only and may run in parallel.
class MeshDistance {
public:
  MeshDistance(const OccHandle<Poly_Triangulation>& mesh, const gp_Trsf& placement = gp_Trsf());

  struct Closest {
    double distance = 0.0; // >= 0
    double signedDistance =
        0.0;      // positive when the point is outside the mesh (in front of the nearest triangle)
    gp_Pnt point; // the nearest point on the mesh
  };
  // Nearest point of the mesh; nullopt for an empty mesh.
  std::optional<Closest> closest(const gp_Pnt& point) const;

  bool empty() const { return _triangles.empty(); }

private:
  struct Node {
    double low[3];
    double high[3];
    int left = -1; // children, or -1 for a leaf
    int right = -1;
    int first = 0; // leaf: range in _order
    int count = 0;
  };
  int build(int first, int count);

  std::vector<gp_XYZ> _points;
  std::vector<std::array<int, 3>> _triangles;
  std::vector<int> _order;
  std::vector<Node> _nodes;
};

} // namespace sc::core
