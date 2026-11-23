#include "core/Comparison.h"

#include "core/Measure.h"
#include "core/MeshDistance.h"
#include "core/PartMesh.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <GProp_PrincipalProps.hxx>
#include <OSD_Parallel.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Mat.hxx>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <deque>
#include <map>
#include <memory>

namespace sc::core {

namespace {

// What decides Unchanged, Moved or Changed: exact properties in the part's own coordinates.
struct Properties {
  bool ok = false;
  double volume = 0.0; // 0 for sheets
  double area = 0.0;
  gp_Pnt centroid;
  gp_Mat inertia;                  // about the centroid
  std::array<double, 3> moments{}; // principal, sorted
};

Properties propertiesOf(const TopoDS_Shape& shape) {
  Properties result;
  if (shape.IsNull()) {
    return result;
  }
  try {
    const bool mesh = measure::hasMeshOnlyFaces(shape);
    GProp_GProps surface;
    BRepGProp::SurfaceProperties(shape, surface, false, mesh);
    result.area = surface.Mass();
    GProp_GProps props = surface;
    if (mesh || TopExp_Explorer(shape, TopAbs_SOLID).More()) {
      GProp_GProps volume;
      BRepGProp::VolumeProperties(shape, volume, false, false, mesh);
      if (std::abs(volume.Mass()) > 1e-12) {
        props = volume;
        result.volume = std::abs(volume.Mass());
      }
    }
    if (std::abs(props.Mass()) <= 1e-12) {
      return result;
    }
    result.centroid = props.CentreOfMass();
    result.inertia = props.MatrixOfInertia();
    const GProp_PrincipalProps principal = props.PrincipalProperties();
    principal.Moments(result.moments[0], result.moments[1], result.moments[2]);
    for (double& moment : result.moments) {
      moment = std::abs(moment);
    }
    std::sort(result.moments.begin(), result.moments.end());
    result.ok = true;
  } catch (const Standard_Failure&) {
    result.ok = false;
  }
  return result;
}

bool close(double a, double b, double relative) {
  return std::abs(a - b) <= relative * std::max(std::abs(a), std::abs(b)) + 1e-9;
}

// The same shape, wherever it is: volume, area and principal moments.
bool sameIntrinsic(const Properties& a, const Properties& b) {
  if (!a.ok || !b.ok) {
    return false;
  }
  const double scale = std::max(a.moments[2], b.moments[2]);
  for (int i = 0; i < 3; ++i) {
    if (std::abs(a.moments[static_cast<size_t>(i)] - b.moments[static_cast<size_t>(i)]) >
        1e-5 * scale + 1e-9) {
      return false;
    }
  }
  return close(a.volume, b.volume, 1e-6) && close(a.area, b.area, 1e-6);
}

// The inertia tensor turned into world axes: R M Rᵀ.
gp_Mat inWorld(const gp_Mat& inertia, const gp_Trsf& placement) {
  const gp_Mat rotation = placement.VectorialPart();
  return rotation * inertia * rotation.Transposed();
}

// The same shape in the same place: the centroid within the tolerance and the inertia tensor in
// world axes equal (which catches a part turned about its own centre).
bool sameInWorld(const Properties& a, const gp_Trsf& placeA, const Properties& b, const gp_Trsf& placeB,
                 double tolerance) {
  if (!sameIntrinsic(a, b)) {
    return false;
  }
  if (a.centroid.Transformed(placeA).Distance(b.centroid.Transformed(placeB)) > tolerance) {
    return false;
  }
  const gp_Mat ma = inWorld(a.inertia, placeA);
  const gp_Mat mb = inWorld(b.inertia, placeB);
  const double scale = std::max(a.moments[2], b.moments[2]);
  for (int row = 1; row <= 3; ++row) {
    for (int column = 1; column <= 3; ++column) {
      if (std::abs(ma(row, column) - mb(row, column)) > 1e-5 * scale + 1e-9) {
        return false;
      }
    }
  }
  return true;
}

// Per-definition caches: properties and meshes are the same for every instance.
class Definitions {
public:
  explicit Definitions(const AssemblyIndex& index) : _index(index) {}

  const Properties& properties(NodeId part) {
    const auto [it, inserted] = _properties.try_emplace(_index.prototypeId(part));
    if (inserted) {
      it->second = propertiesOf(_index.prototypeShape(part));
    }
    return it->second;
  }

  OccHandle<Poly_Triangulation> mesh(NodeId part) {
    const auto [it, inserted] = _meshes.try_emplace(_index.prototypeId(part));
    if (inserted) {
      // Refined like the wall thickness map, so large faces get points inside them.
      it->second = partMesh(_index.prototypeShape(part), MeshRefinement{1.0 / 150.0, 4, 200000});
    }
    return it->second;
  }

private:
  const AssemblyIndex& _index;
  std::map<std::string, Properties> _properties;
  std::map<std::string, OccHandle<Poly_Triangulation>> _meshes;
};

// The deviation map of a changed part and the largest distance either way.
void measureDeviation(PartChange& change, const OccHandle<Poly_Triangulation>& currentMesh,
                      const gp_Trsf& placeCurrent, const OccHandle<Poly_Triangulation>& otherMesh,
                      const gp_Trsf& placeOther) {
  change.mesh = currentMesh;
  if (currentMesh.IsNull() || otherMesh.IsNull()) {
    return;
  }
  const MeshDistance toOther(otherMesh, placeOther);
  const MeshDistance toCurrent(currentMesh, placeCurrent);
  const int count = currentMesh->NbNodes();
  change.deviation.assign(static_cast<size_t>(count), 0.0F);
  OSD_Parallel::For(0, count, [&](int i) {
    const auto closest = toOther.closest(currentMesh->Node(i + 1).Transformed(placeCurrent));
    change.deviation[static_cast<size_t>(i)] = closest ? static_cast<float>(closest->signedDistance) : 0.0F;
  });
  double largest = 0.0;
  for (const float value : change.deviation) {
    largest = std::max(largest, static_cast<double>(std::abs(value)));
  }
  // The other way round: material the current revision lost lies away from its surface.
  const int otherCount = otherMesh->NbNodes();
  std::vector<double> back(static_cast<size_t>(otherCount), 0.0);
  OSD_Parallel::For(0, otherCount, [&](int i) {
    const auto closest = toCurrent.closest(otherMesh->Node(i + 1).Transformed(placeOther));
    back[static_cast<size_t>(i)] = closest ? closest->distance : 0.0;
  });
  for (const double value : back) {
    largest = std::max(largest, value);
  }
  change.maxDeviation = largest;
}

} // namespace

const char* toString(ChangeKind kind) {
  switch (kind) {
  case ChangeKind::Unchanged:
    return "Unchanged";
  case ChangeKind::Moved:
    return "Moved";
  case ChangeKind::Changed:
    return "Changed";
  case ChangeKind::Added:
    return "Added";
  case ChangeKind::Removed:
    return "Removed";
  }
  return "Unknown";
}

std::string pairingPath(const AssemblyIndex& index, NodeId part) {
  std::vector<const std::string*> names;
  for (NodeId node = part; node != NoNode; node = index.node(node).parent) {
    names.push_back(&index.node(node).name);
  }
  if (names.size() > 1) {
    names.pop_back(); // the root: often just the file name
  }
  std::string path;
  for (auto it = names.rbegin(); it != names.rend(); ++it) {
    if (!path.empty()) {
      path += " / ";
    }
    path += **it;
  }
  return path;
}

namespace {

bool hasGeometry(const AssemblyIndex& index, NodeId part) {
  const TopoDS_Shape shape = index.prototypeShape(part);
  return !shape.IsNull() && TopExp_Explorer(shape, TopAbs_FACE).More();
}

// "Nut_2" and "NUT" name the same part: lower case, without an instance number at the end.
std::string partName(const AssemblyIndex& index, NodeId part) {
  std::string name;
  for (const char c : index.node(part).name) {
    name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  const size_t underscore = name.find_last_of('_');
  if (underscore != std::string::npos && underscore + 1 < name.size() &&
      std::all_of(name.begin() + static_cast<std::ptrdiff_t>(underscore) + 1, name.end(),
                  [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
    name.resize(underscore);
  }
  return name;
}

// Stages 2 and 3: what the paths left unpaired, by the same shape nearest in place, then by name.
void pairLeftovers(const AssemblyIndex& current, const AssemblyIndex& other, std::vector<NodeId>& currentLeft,
                   std::vector<NodeId>& otherLeft, Definitions& currentDefinitions,
                   Definitions& otherDefinitions, std::vector<std::pair<NodeId, NodeId>>& pairs) {
  struct Candidate {
    double distance;
    size_t a;
    size_t b;
  };
  std::vector<Candidate> candidates;
  for (size_t i = 0; i < currentLeft.size(); ++i) {
    const Properties& pa = currentDefinitions.properties(currentLeft[i]);
    for (size_t j = 0; j < otherLeft.size(); ++j) {
      const Properties& pb = otherDefinitions.properties(otherLeft[j]);
      if (sameIntrinsic(pa, pb)) {
        const gp_Pnt ca = pa.centroid.Transformed(current.node(currentLeft[i]).location.Transformation());
        const gp_Pnt cb = pb.centroid.Transformed(other.node(otherLeft[j]).location.Transformation());
        candidates.push_back({ca.Distance(cb), i, j});
      }
    }
  }
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const Candidate& x, const Candidate& y) { return x.distance < y.distance; });
  std::vector<char> usedA(currentLeft.size(), 0);
  std::vector<char> usedB(otherLeft.size(), 0);
  for (const Candidate& candidate : candidates) {
    if (usedA[candidate.a] == 0 && usedB[candidate.b] == 0) {
      usedA[candidate.a] = usedB[candidate.b] = 1;
      pairs.emplace_back(currentLeft[candidate.a], otherLeft[candidate.b]);
    }
  }
  std::map<std::string, std::deque<size_t>> byName;
  for (size_t j = 0; j < otherLeft.size(); ++j) {
    if (usedB[j] == 0) {
      byName[partName(other, otherLeft[j])].push_back(j);
    }
  }
  for (size_t i = 0; i < currentLeft.size(); ++i) {
    if (usedA[i] != 0) {
      continue;
    }
    auto it = byName.find(partName(current, currentLeft[i]));
    if (it != byName.end() && !it->second.empty()) {
      usedA[i] = usedB[it->second.front()] = 1;
      pairs.emplace_back(currentLeft[i], otherLeft[it->second.front()]);
      it->second.pop_front();
    }
  }
  std::vector<NodeId> keepA;
  std::vector<NodeId> keepB;
  for (size_t i = 0; i < currentLeft.size(); ++i) {
    if (usedA[i] == 0) {
      keepA.push_back(currentLeft[i]);
    }
  }
  for (size_t j = 0; j < otherLeft.size(); ++j) {
    if (usedB[j] == 0) {
      keepB.push_back(otherLeft[j]);
    }
  }
  currentLeft = std::move(keepA);
  otherLeft = std::move(keepB);
}

// All four stages; the pairs are put back in the current revision's tree order, the added parts
// among them, then the removed ones in the other's tree order.
std::vector<std::pair<NodeId, NodeId>> pairAll(const AssemblyIndex& current, const AssemblyIndex& other,
                                               Definitions& currentDefinitions,
                                               Definitions& otherDefinitions) {
  std::map<std::string, std::deque<NodeId>> byPath;
  for (const NodeId part : other.partInstances()) {
    if (hasGeometry(other, part)) {
      byPath[pairingPath(other, part)].push_back(part);
    }
  }
  std::vector<std::pair<NodeId, NodeId>> pairs;
  std::vector<NodeId> currentLeft;
  std::vector<char> otherTaken(other.nodes().size(), 0);
  for (const NodeId part : current.partInstances()) {
    if (!hasGeometry(current, part)) {
      continue;
    }
    auto it = byPath.find(pairingPath(current, part));
    if (it == byPath.end() || it->second.empty()) {
      currentLeft.push_back(part);
      continue;
    }
    otherTaken[static_cast<size_t>(it->second.front())] = 1;
    pairs.emplace_back(part, it->second.front());
    it->second.pop_front();
  }
  std::vector<NodeId> otherLeft;
  for (const NodeId part : other.partInstances()) {
    if (hasGeometry(other, part) && otherTaken[static_cast<size_t>(part)] == 0) {
      otherLeft.push_back(part);
    }
  }
  pairLeftovers(current, other, currentLeft, otherLeft, currentDefinitions, otherDefinitions, pairs);
  for (const NodeId part : currentLeft) {
    pairs.emplace_back(part, NoNode);
  }
  std::stable_sort(pairs.begin(), pairs.end(), [](const auto& x, const auto& y) {
    if (x.first == NoNode || y.first == NoNode) {
      return x.first != NoNode && y.first == NoNode;
    }
    return x.first < y.first; // NodeIds follow the tree order
  });
  for (const NodeId part : otherLeft) {
    pairs.emplace_back(NoNode, part);
  }
  return pairs;
}

} // namespace

std::vector<std::pair<NodeId, NodeId>> pairParts(const AssemblyIndex& current, const AssemblyIndex& other) {
  Definitions currentDefinitions(current);
  Definitions otherDefinitions(other);
  return pairAll(current, other, currentDefinitions, otherDefinitions);
}

ComparisonResult compareModels(const AssemblyIndex& current, const AssemblyIndex& other,
                               const ComparisonOptions& options, const ProgressCallback& progress,
                               const CancelToken* cancel) {
  ComparisonResult result;
  Definitions currentDefinitions(current);
  Definitions otherDefinitions(other);
  const std::vector<std::pair<NodeId, NodeId>> pairs =
      pairAll(current, other, currentDefinitions, otherDefinitions);
  for (size_t i = 0; i < pairs.size(); ++i) {
    if (cancel != nullptr && cancel->isCancelled()) {
      result.cancelled = true;
      return result;
    }
    if (progress) {
      progress(static_cast<double>(i) / static_cast<double>(pairs.size()), "Comparing parts");
    }
    const auto [a, b] = pairs[i];
    PartChange change;
    change.current = a;
    change.other = b;
    change.path = a != NoNode ? pairingPath(current, a) : pairingPath(other, b);
    if (b == NoNode) {
      change.kind = ChangeKind::Added;
    } else if (a == NoNode) {
      change.kind = ChangeKind::Removed;
    } else {
      const Properties& pa = currentDefinitions.properties(a);
      const Properties& pb = otherDefinitions.properties(b);
      const gp_Trsf placeA = current.node(a).location.Transformation();
      const gp_Trsf placeB = other.node(b).location.Transformation();
      if (sameInWorld(pa, placeA, pb, placeB, options.tolerance)) {
        change.kind = ChangeKind::Unchanged;
      } else if (sameIntrinsic(pa, pb)) {
        change.kind = ChangeKind::Moved;
      } else {
        change.kind = ChangeKind::Changed;
        measureDeviation(change, currentDefinitions.mesh(a), placeA, otherDefinitions.mesh(b), placeB);
        result.maxDeviation = std::max(result.maxDeviation, change.maxDeviation);
      }
    }
    switch (change.kind) {
    case ChangeKind::Unchanged:
      ++result.unchanged;
      break;
    case ChangeKind::Moved:
      ++result.moved;
      break;
    case ChangeKind::Changed:
      ++result.changed;
      break;
    case ChangeKind::Added:
      ++result.added;
      break;
    case ChangeKind::Removed:
      ++result.removed;
      break;
    }
    result.changes.push_back(std::move(change));
  }
  if (progress) {
    progress(1.0, "Comparing parts");
  }
  return result;
}

} // namespace sc::core
