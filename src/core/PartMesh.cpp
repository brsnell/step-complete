#include "core/PartMesh.h"

#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Poly_Triangle.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

namespace sc::core {

namespace {

void refine(std::vector<gp_Pnt>& points, std::vector<gp_Vec>& normals,
            std::vector<std::array<int, 3>>& triangles, const MeshRefinement& refinement) {
  if (refinement.longestFraction <= 0.0 || refinement.levels <= 0 || points.empty()) {
    return;
  }
  Bnd_Box box;
  for (const gp_Pnt& p : points) {
    box.Add(p);
  }
  const double longest = std::sqrt(box.SquareExtent()) * refinement.longestFraction;
  for (int level = 0; level < refinement.levels; ++level) {
    std::map<std::pair<int, int>, int> midpoints;
    std::vector<std::array<int, 3>> next;
    next.reserve(triangles.size());
    bool split = false;
    const auto midpoint = [&](int a, int b) {
      const std::pair<int, int> key = std::minmax(a, b);
      const auto [it, inserted] = midpoints.try_emplace(key, static_cast<int>(points.size()));
      if (inserted) {
        const gp_Pnt& pa = points[static_cast<size_t>(a)];
        const gp_Pnt& pb = points[static_cast<size_t>(b)];
        points.emplace_back((pa.XYZ() + pb.XYZ()) * 0.5);
        gp_Vec normal =
            normals[static_cast<size_t>(a)].Normalized() + normals[static_cast<size_t>(b)].Normalized();
        normals.push_back(normal);
      }
      return it->second;
    };
    for (const std::array<int, 3>& t : triangles) {
      const gp_Pnt& a = points[static_cast<size_t>(t[0])];
      const gp_Pnt& b = points[static_cast<size_t>(t[1])];
      const gp_Pnt& c = points[static_cast<size_t>(t[2])];
      const double edge = std::max({a.Distance(b), b.Distance(c), c.Distance(a)});
      if (edge <= longest || static_cast<int>(points.size()) + 3 > refinement.maxPoints) {
        next.push_back(t);
        continue;
      }
      split = true;
      const int ab = midpoint(t[0], t[1]);
      const int bc = midpoint(t[1], t[2]);
      const int ca = midpoint(t[2], t[0]);
      next.push_back({t[0], ab, ca});
      next.push_back({ab, t[1], bc});
      next.push_back({ca, bc, t[2]});
      next.push_back({ab, bc, ca});
    }
    triangles = std::move(next);
    if (!split) {
      return;
    }
  }
}

} // namespace

OccHandle<Poly_Triangulation> partMesh(const TopoDS_Shape& shape, const MeshRefinement& refinement) {
  std::vector<gp_Pnt> points;
  std::vector<gp_Vec> normals;
  std::vector<std::array<int, 3>> triangles; // 0-based
  for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
    const TopoDS_Face& face = TopoDS::Face(it.Current());
    TopLoc_Location location;
    const OccHandle<Poly_Triangulation> triangulation = BRep_Tool::Triangulation(face, location);
    if (triangulation.IsNull() || triangulation->NbTriangles() == 0) {
      continue;
    }
    const gp_Trsf trsf = location.Transformation();
    const int base = static_cast<int>(points.size());
    for (int i = 1; i <= triangulation->NbNodes(); ++i) {
      points.push_back(triangulation->Node(i).Transformed(trsf));
      normals.emplace_back(0.0, 0.0, 0.0);
    }
    const bool reversed = face.Orientation() == TopAbs_REVERSED;
    for (int i = 1; i <= triangulation->NbTriangles(); ++i) {
      int n1 = 0;
      int n2 = 0;
      int n3 = 0;
      triangulation->Triangle(i).Get(n1, n2, n3);
      if (reversed) {
        std::swap(n2, n3);
      }
      const std::array<int, 3> triangle = {base + n1 - 1, base + n2 - 1, base + n3 - 1};
      const gp_Pnt& a = points[static_cast<size_t>(triangle[0])];
      const gp_Vec area = gp_Vec(a, points[static_cast<size_t>(triangle[1])])
                              .Crossed(gp_Vec(a, points[static_cast<size_t>(triangle[2])])); // area-weighted
      for (const int n : triangle) {
        normals[static_cast<size_t>(n)] += area;
      }
      triangles.push_back(triangle);
    }
  }
  if (triangles.empty()) {
    return nullptr;
  }
  refine(points, normals, triangles, refinement);
  OccHandle<Poly_Triangulation> mesh = new Poly_Triangulation(
      static_cast<int>(points.size()), static_cast<int>(triangles.size()), false, true);
  for (size_t i = 0; i < points.size(); ++i) {
    const int node = static_cast<int>(i) + 1;
    mesh->SetNode(node, points[i]);
    const gp_Vec& normal = normals[i];
    mesh->SetNormal(node, normal.Magnitude() > 0.0 ? gp_Dir(normal) : gp_Dir(0.0, 0.0, 1.0));
  }
  for (size_t i = 0; i < triangles.size(); ++i) {
    const std::array<int, 3>& t = triangles[i];
    mesh->SetTriangle(static_cast<int>(i) + 1, Poly_Triangle(t[0] + 1, t[1] + 1, t[2] + 1));
  }
  return mesh;
}

} // namespace sc::core
