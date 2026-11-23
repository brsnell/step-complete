#include "core/WallThickness.h"

#include <BRep_Tool.hxx>
#include <OSD_Parallel.hxx>
#include <Poly_Triangle.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <utility>

namespace sc::core {

namespace {

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

Vec3 operator+(const Vec3& a, const Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vec3 operator-(const Vec3& a, const Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec3 operator*(const Vec3& a, double s) {
  return {a.x * s, a.y * s, a.z * s};
}
double dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double length(const Vec3& a) {
  return std::sqrt(dot(a, a));
}
Vec3 normalized(const Vec3& a) {
  const double l = length(a);
  return l > 0.0 ? a * (1.0 / l) : Vec3{0.0, 0.0, 1.0};
}
Vec3 toVec(const gp_XYZ& p) {
  return {p.X(), p.Y(), p.Z()};
}

using Triangle = std::array<int, 3>; // 0-based node indices

// A ray leaves through a triangle only if it meets it at most ~84 degrees from its normal.
constexpr double MinExitCosine = 0.1;
// Hits closer than this part of the part's size are the ray's own surface, not a wall.
constexpr double MinWallFraction = 1e-5;
// Share of points left out at each end of a map's range.
constexpr double RangeTrim = 0.001;

// A mesh being built: one entry per node, triangles wound outward.
struct Mesh {
  std::vector<Vec3> points;
  std::vector<Vec3> normals;
  std::vector<Triangle> triangles;
};

// Collects every face's triangulation in the shape's coordinates, winding triangles outward
// (reversed faces swap two corners) and giving each node the average of its face's triangle
// normals. Faces keep their own nodes, so sharp edges keep sharp normals.
Mesh collect(const TopoDS_Shape& shape) {
  Mesh mesh;
  for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
    const TopoDS_Face& face = TopoDS::Face(it.Current());
    TopLoc_Location location;
    const OccHandle<Poly_Triangulation> triangulation = BRep_Tool::Triangulation(face, location);
    if (triangulation.IsNull() || triangulation->NbTriangles() == 0) {
      continue;
    }
    const gp_Trsf trsf = location.Transformation();
    const int base = static_cast<int>(mesh.points.size());
    for (int i = 1; i <= triangulation->NbNodes(); ++i) {
      mesh.points.push_back(toVec(triangulation->Node(i).Transformed(trsf).XYZ()));
      mesh.normals.push_back({});
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
      const Triangle triangle = {base + n1 - 1, base + n2 - 1, base + n3 - 1};
      const Vec3& a = mesh.points[static_cast<size_t>(triangle[0])];
      const Vec3 area = cross(mesh.points[static_cast<size_t>(triangle[1])] - a,
                              mesh.points[static_cast<size_t>(triangle[2])] - a); // area-weighted
      for (const int n : triangle) {
        mesh.normals[static_cast<size_t>(n)] = mesh.normals[static_cast<size_t>(n)] + area;
      }
      mesh.triangles.push_back(triangle);
    }
  }
  for (Vec3& normal : mesh.normals) {
    normal = normalized(normal);
  }
  return mesh;
}

// Splits triangles with an edge longer than 'longest' into four, level by level, until none is
// or the point cap is reached. Midpoints are shared between the two triangles of an edge.
void refine(Mesh& mesh, double longest, int levels, int maxPoints) {
  for (int level = 0; level < levels; ++level) {
    std::map<std::pair<int, int>, int> midpoints;
    std::vector<Triangle> next;
    next.reserve(mesh.triangles.size());
    bool split = false;
    const auto midpoint = [&mesh, &midpoints](int a, int b) {
      const std::pair<int, int> key = std::minmax(a, b);
      const auto [it, inserted] = midpoints.try_emplace(key, static_cast<int>(mesh.points.size()));
      if (inserted) {
        mesh.points.push_back((mesh.points[static_cast<size_t>(a)] + mesh.points[static_cast<size_t>(b)]) *
                              0.5);
        mesh.normals.push_back(
            normalized(mesh.normals[static_cast<size_t>(a)] + mesh.normals[static_cast<size_t>(b)]));
      }
      return it->second;
    };
    for (const Triangle& t : mesh.triangles) {
      const Vec3& a = mesh.points[static_cast<size_t>(t[0])];
      const Vec3& b = mesh.points[static_cast<size_t>(t[1])];
      const Vec3& c = mesh.points[static_cast<size_t>(t[2])];
      const double edge = std::max({length(b - a), length(c - b), length(a - c)});
      if (edge <= longest || static_cast<int>(mesh.points.size()) + 3 > maxPoints) {
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
    mesh.triangles = std::move(next);
    if (!split) {
      return;
    }
  }
}

} // namespace

// A median-split bounding-volume hierarchy over a mesh's triangles, for nearest-hit ray queries.
class MeshBvh {
public:
  explicit MeshBvh(Mesh mesh) : _mesh(std::move(mesh)) {
    _order.resize(_mesh.triangles.size());
    std::iota(_order.begin(), _order.end(), 0);
    _centroids.reserve(_mesh.triangles.size());
    for (const Triangle& t : _mesh.triangles) {
      _centroids.push_back((point(t[0]) + point(t[1]) + point(t[2])) * (1.0 / 3.0));
    }
    if (!_order.empty()) {
      build(0, static_cast<int>(_order.size()));
    }
  }

  struct Hit {
    double t = 0.0;
    int triangle = -1;
  };

  // The nearest triangle hit at t > tMin. With 'exitOnly', only triangles facing away from the
  // ray (where it leaves the part) count.
  std::optional<Hit> cast(const Vec3& origin, const Vec3& direction, double tMin, bool exitOnly) const {
    if (_nodes.empty()) {
      return std::nullopt;
    }
    const Vec3 inverse = {1.0 / direction.x, 1.0 / direction.y, 1.0 / direction.z};
    Hit best;
    best.t = std::numeric_limits<double>::infinity();
    std::array<int, 64> stack{};
    int top = 0;
    stack[static_cast<size_t>(top++)] = 0;
    while (top > 0) {
      const Node& node = _nodes[static_cast<size_t>(stack[static_cast<size_t>(--top)])];
      if (!slab(node, origin, inverse, best.t)) {
        continue;
      }
      if (node.count > 0) {
        for (int i = node.first; i < node.first + node.count; ++i) {
          const int triangle = _order[static_cast<size_t>(i)];
          const std::optional<double> t = intersect(triangle, origin, direction, exitOnly);
          if (t && *t > tMin && *t < best.t) {
            best = {*t, triangle};
          }
        }
      } else if (top + 2 <= static_cast<int>(stack.size())) {
        stack[static_cast<size_t>(top++)] = node.left;
        stack[static_cast<size_t>(top++)] = node.right;
      }
    }
    if (best.triangle < 0) {
      return std::nullopt;
    }
    return best;
  }

  const Mesh& mesh() const { return _mesh; }

  Vec3 normalOf(int triangle) const {
    const Triangle& t = _mesh.triangles[static_cast<size_t>(triangle)];
    return normalized(cross(point(t[1]) - point(t[0]), point(t[2]) - point(t[0])));
  }

private:
  struct Node {
    Vec3 low;
    Vec3 high;
    int left = 0; // children, when an inner node
    int right = 0;
    int first = 0; // leaf: first entry in _order
    int count = 0; // leaf: number of triangles (0 for an inner node)
  };

  const Vec3& point(int n) const { return _mesh.points[static_cast<size_t>(n)]; }

  int build(int first, int count) {
    const int index = static_cast<int>(_nodes.size());
    _nodes.emplace_back();
    Vec3 low = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                std::numeric_limits<double>::max()};
    Vec3 high = low * -1.0;
    for (int i = first; i < first + count; ++i) {
      for (const int n : _mesh.triangles[static_cast<size_t>(_order[static_cast<size_t>(i)])]) {
        const Vec3& p = point(n);
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
      }
    }
    _nodes[static_cast<size_t>(index)].low = low;
    _nodes[static_cast<size_t>(index)].high = high;
    if (count <= 4) {
      _nodes[static_cast<size_t>(index)].first = first;
      _nodes[static_cast<size_t>(index)].count = count;
      return index;
    }
    const Vec3 extent = high - low;
    const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : (extent.y >= extent.z ? 1 : 2);
    const auto key = [this, axis](int triangle) {
      const Vec3& c = _centroids[static_cast<size_t>(triangle)];
      return axis == 0 ? c.x : (axis == 1 ? c.y : c.z);
    };
    const int middle = first + count / 2;
    std::nth_element(_order.begin() + first, _order.begin() + middle, _order.begin() + first + count,
                     [&key](int a, int b) { return key(a) < key(b); });
    const int left = build(first, middle - first);
    const int right = build(middle, first + count - middle);
    _nodes[static_cast<size_t>(index)].left = left; // _nodes may have grown: index again
    _nodes[static_cast<size_t>(index)].right = right;
    return index;
  }

  static bool slab(const Node& node, const Vec3& origin, const Vec3& inverse, double tMax) {
    double t0 = 0.0;
    double t1 = tMax;
    const double lows[3] = {node.low.x, node.low.y, node.low.z};
    const double highs[3] = {node.high.x, node.high.y, node.high.z};
    const double origins[3] = {origin.x, origin.y, origin.z};
    const double inverses[3] = {inverse.x, inverse.y, inverse.z};
    for (int axis = 0; axis < 3; ++axis) {
      double entering = (lows[axis] - origins[axis]) * inverses[axis];
      double leaving = (highs[axis] - origins[axis]) * inverses[axis];
      if (entering > leaving) {
        std::swap(entering, leaving);
      }
      if (std::isnan(entering) || std::isnan(leaving)) { // origin on the slab with a zero direction
        if (origins[axis] < lows[axis] || origins[axis] > highs[axis]) {
          return false;
        }
        continue;
      }
      t0 = std::max(t0, entering);
      t1 = std::min(t1, leaving * (1.0 + 1e-9) + 1e-12);
      if (t0 > t1) {
        return false;
      }
    }
    return true;
  }

  // Möller-Trumbore; with exitOnly, triangles facing the ray are skipped.
  std::optional<double> intersect(int triangle, const Vec3& origin, const Vec3& direction,
                                  bool exitOnly) const {
    const Triangle& t = _mesh.triangles[static_cast<size_t>(triangle)];
    const Vec3& a = point(t[0]);
    const Vec3 e1 = point(t[1]) - a;
    const Vec3 e2 = point(t[2]) - a;
    if (exitOnly) {
      // Leaving through a wall that faces away from the ray, not grazing one: a ray from a node on
      // a concave sharp edge runs along the next face, and a hit there is not a wall.
      const Vec3 normal = cross(e1, e2);
      const double facing = dot(normal, direction);
      if (facing <= MinExitCosine * length(normal)) {
        return std::nullopt;
      }
    }
    const Vec3 p = cross(direction, e2);
    const double det = dot(e1, p);
    if (std::abs(det) < 1e-18) {
      return std::nullopt; // parallel
    }
    const double inverse = 1.0 / det;
    const Vec3 s = origin - a;
    const double u = dot(s, p) * inverse;
    if (u < -1e-9 || u > 1.0 + 1e-9) {
      return std::nullopt;
    }
    const Vec3 q = cross(s, e1);
    const double v = dot(direction, q) * inverse;
    if (v < -1e-9 || u + v > 1.0 + 1e-9) {
      return std::nullopt;
    }
    return dot(e2, q) * inverse;
  }

  Mesh _mesh;
  std::vector<Vec3> _centroids;
  std::vector<int> _order;
  std::vector<Node> _nodes;
};

namespace {

// Size of the mesh: the diagonal of its box.
double sizeOf(const Mesh& mesh) {
  if (mesh.points.empty()) {
    return 0.0;
  }
  Vec3 low = mesh.points.front();
  Vec3 high = low;
  for (const Vec3& p : mesh.points) {
    low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
    high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
  }
  return length(high - low);
}

// Inward ray from a surface point with the given outward normal, to where it leaves the part.
std::optional<double> inward(const MeshBvh& bvh, const Vec3& point, const Vec3& normal, double epsilon) {
  const Vec3 direction = normal * -1.0;
  const std::optional<MeshBvh::Hit> hit = bvh.cast(point, direction, epsilon, true);
  if (!hit) {
    return std::nullopt;
  }
  return hit->t;
}

} // namespace

std::shared_ptr<const ThicknessMap>
computeThickness(const TopoDS_Shape& shape, const ThicknessOptions& options, const CancelToken* cancel) {
  if (shape.IsNull()) {
    return nullptr;
  }
  Mesh mesh = collect(shape);
  if (mesh.triangles.empty()) {
    return nullptr;
  }
  const double size = sizeOf(mesh);
  refine(mesh, size * options.refineFraction, options.refineLevels, options.maxPoints);
  auto bvh = std::make_shared<const MeshBvh>(std::move(mesh));
  const Mesh& built = bvh->mesh();
  const double epsilon = std::max(size * MinWallFraction, 1e-9);

  auto map = std::make_shared<ThicknessMap>();
  const int nodeCount = static_cast<int>(built.points.size());
  map->thickness.assign(built.points.size(), std::numeric_limits<float>::quiet_NaN());
  OSD_Parallel::For(0, nodeCount, [&](int i) {
    if (cancel != nullptr && cancel->isCancelled()) {
      return;
    }
    const std::optional<double> t =
        inward(*bvh, built.points[static_cast<size_t>(i)], built.normals[static_cast<size_t>(i)], epsilon);
    if (t) {
      map->thickness[static_cast<size_t>(i)] = static_cast<float>(*t);
    }
  });
  if (cancel != nullptr && cancel->isCancelled()) {
    return nullptr;
  }

  OccHandle<Poly_Triangulation> triangulation =
      new Poly_Triangulation(nodeCount, static_cast<int>(built.triangles.size()), false, true);
  for (int i = 0; i < nodeCount; ++i) {
    const Vec3& p = built.points[static_cast<size_t>(i)];
    const Vec3& n = built.normals[static_cast<size_t>(i)];
    triangulation->SetNode(i + 1, gp_Pnt(p.x, p.y, p.z));
    triangulation->SetNormal(i + 1, gp_Dir(n.x, n.y, n.z));
  }
  for (size_t i = 0; i < built.triangles.size(); ++i) {
    const Triangle& t = built.triangles[i];
    triangulation->SetTriangle(static_cast<int>(i) + 1, Poly_Triangle(t[0] + 1, t[1] + 1, t[2] + 1));
  }
  map->mesh = triangulation;
  map->bvh = bvh;
  // The range ignores the most extreme 0.1% of points at each end: single points at sharp corners
  // of curved faces can read far off, and one of them would set the whole color scale.
  std::vector<float> values;
  values.reserve(map->thickness.size());
  for (const float value : map->thickness) {
    if (!std::isnan(value)) {
      values.push_back(value);
    }
  }
  map->valued = static_cast<int>(values.size());
  if (!values.empty()) {
    const size_t low = static_cast<size_t>(std::floor(RangeTrim * static_cast<double>(values.size())));
    const size_t high = values.size() - 1 - low;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(low), values.end());
    map->min = values[low];
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(high), values.end());
    map->max = values[high];
  }
  return map;
}

std::optional<double> thicknessAt(const ThicknessMap& map, const gp_Pnt& point, const gp_Dir& direction) {
  if (!map.bvh) {
    return std::nullopt;
  }
  const MeshBvh& bvh = *map.bvh;
  const double size = sizeOf(bvh.mesh());
  const Vec3 ray = toVec(direction.XYZ());
  // Back off a little and look along the view: the first triangle hit is the surface under the point.
  const Vec3 start = toVec(point.XYZ()) - ray * (size * 1e-3);
  const std::optional<MeshBvh::Hit> surface = bvh.cast(start, ray, 0.0, false);
  if (!surface) {
    return std::nullopt;
  }
  Vec3 normal = bvh.normalOf(surface->triangle);
  if (dot(normal, ray) > 0.0) {
    normal = normal * -1.0; // seen from inside (a section cut): go the other way
  }
  return inward(bvh, start + ray * surface->t, normal, std::max(size * MinWallFraction, 1e-9));
}

std::shared_ptr<const ThicknessMap> ThicknessResult::mapOf(const AssemblyIndex& index, NodeId part) const {
  const auto it = maps.find(index.prototypeId(part));
  return it == maps.end() ? nullptr : it->second;
}

ThicknessResult analyseThickness(const AssemblyIndex& index, const std::vector<NodeId>& nodes,
                                 const ThicknessOptions& options, const ProgressCallback& progress,
                                 const CancelToken* cancel) {
  ThicknessResult result;
  for (const NodeId node : nodes) {
    if (node >= 0 && static_cast<size_t>(node) < index.nodes().size()) {
      const std::vector<NodeId> below = index.partsBelow(node);
      result.parts.insert(result.parts.end(), below.begin(), below.end());
    }
  }
  std::sort(result.parts.begin(), result.parts.end());
  result.parts.erase(std::unique(result.parts.begin(), result.parts.end()), result.parts.end());

  std::vector<NodeId> firsts; // one instance per definition
  for (const NodeId part : result.parts) {
    if (result.maps.emplace(index.prototypeId(part), nullptr).second) {
      firsts.push_back(part);
    }
  }
  result.definitions = static_cast<int>(firsts.size());
  bool any = false;
  for (size_t i = 0; i < firsts.size(); ++i) {
    if (cancel != nullptr && cancel->isCancelled()) {
      result.cancelled = true;
      return result;
    }
    if (progress) {
      progress(static_cast<double>(i) / static_cast<double>(firsts.size()), "Measuring wall thickness");
    }
    std::shared_ptr<const ThicknessMap> map;
    try {
      map = computeThickness(index.prototypeShape(firsts[i]), options, cancel);
    } catch (const Standard_Failure&) {
      map = nullptr;
    }
    if (cancel != nullptr && cancel->isCancelled()) {
      result.cancelled = true;
      return result;
    }
    if (map) {
      result.points += map->mesh->NbNodes();
      result.valued += map->valued;
    }
    if (!map || map->valued == 0) {
      ++result.withoutValue;
    } else {
      result.min = any ? std::min(result.min, map->min) : map->min;
      result.max = any ? std::max(result.max, map->max) : map->max;
      any = true;
    }
    result.maps[index.prototypeId(firsts[i])] = std::move(map);
  }
  if (progress) {
    progress(1.0, "Measuring wall thickness");
  }
  return result;
}

} // namespace sc::core
