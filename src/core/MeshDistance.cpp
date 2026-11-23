#include "core/MeshDistance.h"

#include <Poly_Triangle.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace sc::core {

namespace {

// The closest point of triangle abc to p (Ericson, Real-Time Collision Detection, 5.1.5).
gp_XYZ closestOnTriangle(const gp_XYZ& p, const gp_XYZ& a, const gp_XYZ& b, const gp_XYZ& c) {
  const gp_XYZ ab = b - a;
  const gp_XYZ ac = c - a;
  const gp_XYZ ap = p - a;
  const double d1 = ab.Dot(ap);
  const double d2 = ac.Dot(ap);
  if (d1 <= 0.0 && d2 <= 0.0) {
    return a;
  }
  const gp_XYZ bp = p - b;
  const double d3 = ab.Dot(bp);
  const double d4 = ac.Dot(bp);
  if (d3 >= 0.0 && d4 <= d3) {
    return b;
  }
  const double vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
    return a + ab * (d1 / (d1 - d3));
  }
  const gp_XYZ cp = p - c;
  const double d5 = ab.Dot(cp);
  const double d6 = ac.Dot(cp);
  if (d6 >= 0.0 && d5 <= d6) {
    return c;
  }
  const double vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
    return a + ac * (d2 / (d2 - d6));
  }
  const double va = d3 * d6 - d5 * d4;
  if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
    return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
  }
  const double denominator = 1.0 / (va + vb + vc);
  return a + ab * (vb * denominator) + ac * (vc * denominator);
}

} // namespace

MeshDistance::MeshDistance(const OccHandle<Poly_Triangulation>& mesh, const gp_Trsf& placement) {
  if (mesh.IsNull()) {
    return;
  }
  _points.reserve(static_cast<size_t>(mesh->NbNodes()));
  for (int i = 1; i <= mesh->NbNodes(); ++i) {
    _points.push_back(mesh->Node(i).Transformed(placement).XYZ());
  }
  // A mirroring placement turns the winding inside out: swap two corners to keep it outward.
  const bool mirrored = placement.IsNegative();
  _triangles.reserve(static_cast<size_t>(mesh->NbTriangles()));
  for (int i = 1; i <= mesh->NbTriangles(); ++i) {
    int n1 = 0;
    int n2 = 0;
    int n3 = 0;
    mesh->Triangle(i).Get(n1, n2, n3);
    _triangles.push_back(mirrored ? std::array<int, 3>{n1 - 1, n3 - 1, n2 - 1}
                                  : std::array<int, 3>{n1 - 1, n2 - 1, n3 - 1});
  }
  _order.resize(_triangles.size());
  std::iota(_order.begin(), _order.end(), 0);
  if (!_order.empty()) {
    build(0, static_cast<int>(_order.size()));
  }
}

int MeshDistance::build(int first, int count) {
  const int index = static_cast<int>(_nodes.size());
  _nodes.emplace_back();
  Node node;
  for (int axis = 0; axis < 3; ++axis) {
    node.low[axis] = std::numeric_limits<double>::max();
    node.high[axis] = std::numeric_limits<double>::lowest();
  }
  for (int i = first; i < first + count; ++i) {
    for (const int n : _triangles[static_cast<size_t>(_order[static_cast<size_t>(i)])]) {
      const gp_XYZ& p = _points[static_cast<size_t>(n)];
      for (int axis = 0; axis < 3; ++axis) {
        node.low[axis] = std::min(node.low[axis], p.Coord(axis + 1));
        node.high[axis] = std::max(node.high[axis], p.Coord(axis + 1));
      }
    }
  }
  if (count <= 4) {
    node.first = first;
    node.count = count;
    _nodes[static_cast<size_t>(index)] = node;
    return index;
  }
  int axis = 0;
  for (int k = 1; k < 3; ++k) {
    if (node.high[k] - node.low[k] > node.high[axis] - node.low[axis]) {
      axis = k;
    }
  }
  const auto centre = [this, axis](int triangle) {
    const auto& t = _triangles[static_cast<size_t>(triangle)];
    return _points[static_cast<size_t>(t[0])].Coord(axis + 1) +
           _points[static_cast<size_t>(t[1])].Coord(axis + 1) +
           _points[static_cast<size_t>(t[2])].Coord(axis + 1);
  };
  const int middle = first + count / 2;
  std::nth_element(_order.begin() + first, _order.begin() + middle, _order.begin() + first + count,
                   [&centre](int a, int b) { return centre(a) < centre(b); });
  node.left = build(first, middle - first);
  node.right = build(middle, first + count - middle);
  _nodes[static_cast<size_t>(index)] = node;
  return index;
}

std::optional<MeshDistance::Closest> MeshDistance::closest(const gp_Pnt& point) const {
  if (_nodes.empty()) {
    return std::nullopt;
  }
  const gp_XYZ p = point.XYZ();
  const auto boxDistance2 = [&p](const Node& node) {
    double sum = 0.0;
    for (int axis = 0; axis < 3; ++axis) {
      const double v = p.Coord(axis + 1);
      const double d =
          v < node.low[axis] ? node.low[axis] - v : (v > node.high[axis] ? v - node.high[axis] : 0.0);
      sum += d * d;
    }
    return sum;
  };
  double best = std::numeric_limits<double>::max();
  int bestTriangle = -1;
  gp_XYZ bestPoint;
  std::vector<int> stack;
  stack.reserve(64);
  stack.push_back(0);
  while (!stack.empty()) {
    const Node& node = _nodes[static_cast<size_t>(stack.back())];
    stack.pop_back();
    if (boxDistance2(node) >= best) {
      continue;
    }
    if (node.left < 0) {
      for (int i = node.first; i < node.first + node.count; ++i) {
        const int triangle = _order[static_cast<size_t>(i)];
        const auto& t = _triangles[static_cast<size_t>(triangle)];
        const gp_XYZ q =
            closestOnTriangle(p, _points[static_cast<size_t>(t[0])], _points[static_cast<size_t>(t[1])],
                              _points[static_cast<size_t>(t[2])]);
        const double d2 = (p - q).SquareModulus();
        if (d2 < best) {
          best = d2;
          bestTriangle = triangle;
          bestPoint = q;
        }
      }
      continue;
    }
    // Nearer child last, so it is searched first.
    const Node& left = _nodes[static_cast<size_t>(node.left)];
    const Node& right = _nodes[static_cast<size_t>(node.right)];
    if (boxDistance2(left) < boxDistance2(right)) {
      stack.push_back(node.right);
      stack.push_back(node.left);
    } else {
      stack.push_back(node.left);
      stack.push_back(node.right);
    }
  }
  if (bestTriangle < 0) {
    return std::nullopt;
  }
  Closest result;
  result.distance = std::sqrt(best);
  result.point = gp_Pnt(bestPoint);
  // The sign from the side of the nearest triangle. At an edge or a vertex the nearest triangle is
  // one of several; its normal still tells outside from inside for a closed mesh.
  const auto& t = _triangles[static_cast<size_t>(bestTriangle)];
  const gp_XYZ& a = _points[static_cast<size_t>(t[0])];
  const gp_XYZ normal =
      (_points[static_cast<size_t>(t[1])] - a).Crossed(_points[static_cast<size_t>(t[2])] - a);
  result.signedDistance = (p - bestPoint).Dot(normal) >= 0.0 ? result.distance : -result.distance;
  return result;
}

} // namespace sc::core
