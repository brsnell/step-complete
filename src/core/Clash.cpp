#include "core/Clash.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <OSD_Parallel.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS_Compound.hxx>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <string>

namespace sc::core {

namespace {

bool hasSolid(const TopoDS_Shape& shape) {
  return TopExp_Explorer(shape, TopAbs_SOLID).More();
}

// The contact points of a touching pair (at most a few dozen), as vertices.
TopoDS_Shape contactPoints(const BRepExtrema_DistShapeShape& distance) {
  BRep_Builder builder;
  TopoDS_Compound points;
  builder.MakeCompound(points);
  const int count = std::min(distance.NbSolution(), 50);
  for (int i = 1; i <= count; ++i) {
    builder.Add(points, BRepBuilderAPI_MakeVertex(distance.PointOnShape1(i)).Vertex());
  }
  return points;
}

int rank(ClashKind kind) {
  return static_cast<int>(kind);
}

} // namespace

const char* toString(ClashKind kind) {
  switch (kind) {
  case ClashKind::Interference:
    return "Interference";
  case ClashKind::Touching:
    return "Touching";
  case ClashKind::Clearance:
    return "Clearance";
  case ClashKind::Failed:
    return "Could not be checked";
  }
  return "Clash";
}

std::vector<std::pair<int, int>> candidatePairs(const std::vector<Bnd_Box>& boxes) {
  std::vector<int> order;
  order.reserve(boxes.size());
  for (int i = 0; i < static_cast<int>(boxes.size()); ++i) {
    if (!boxes[static_cast<size_t>(i)].IsVoid()) {
      order.push_back(i);
    }
  }
  const auto xMin = [&boxes](int i) { return boxes[static_cast<size_t>(i)].CornerMin().X(); };
  std::sort(order.begin(), order.end(), [&xMin](int a, int b) { return xMin(a) < xMin(b); });

  std::vector<std::pair<int, int>> pairs;
  std::vector<int> active; // boxes whose X range may still reach the current one
  for (const int current : order) {
    const Bnd_Box& box = boxes[static_cast<size_t>(current)];
    const double start = box.CornerMin().X();
    active.erase(std::remove_if(active.begin(), active.end(),
                                [&boxes, start](int other) {
                                  return boxes[static_cast<size_t>(other)].CornerMax().X() < start;
                                }),
                 active.end());
    for (const int other : active) {
      if (!box.IsOut(boxes[static_cast<size_t>(other)])) {
        pairs.emplace_back(std::min(current, other), std::max(current, other));
      }
    }
    active.push_back(current);
  }
  std::sort(pairs.begin(), pairs.end());
  return pairs;
}

std::optional<Clash> checkPair(const TopoDS_Shape& a, const TopoDS_Shape& b, const ClashOptions& options) {
  Clash clash;
  try {
    BRepExtrema_DistShapeShape distance(a, b, Extrema_ExtFlag_MIN);
    if (!distance.IsDone() || distance.NbSolution() == 0) {
      clash.kind = ClashKind::Failed;
      return clash;
    }
    const double gap = distance.Value();
    if (gap > TouchTolerance) {
      if (!options.clearance || gap >= *options.clearance) {
        return std::nullopt;
      }
      clash.kind = ClashKind::Clearance;
      clash.distance = gap;
      const gp_Pnt p1 = distance.PointOnShape1(1);
      const gp_Pnt p2 = distance.PointOnShape2(1);
      clash.witness = BRepBuilderAPI_MakeEdge(p1, p2).Edge();
      return clash;
    }

    if (hasSolid(a) && hasSolid(b)) {
      // Non-destructive: the inputs share their geometry with the 3D view and other instances.
      BRepAlgoAPI_Common common;
      TopTools_ListOfShape arguments;
      arguments.Append(a);
      TopTools_ListOfShape tools;
      tools.Append(b);
      common.SetArguments(arguments);
      common.SetTools(tools);
      common.SetNonDestructive(true);
      common.SetRunParallel(false);
      common.Build();
      if (common.HasErrors() || !common.IsDone()) {
        clash.kind = ClashKind::Failed;
        return clash;
      }
      GProp_GProps props;
      BRepGProp::VolumeProperties(common.Shape(), props);
      if (props.Mass() > OverlapTolerance) {
        clash.kind = ClashKind::Interference;
        clash.volume = props.Mass();
        clash.witness = common.Shape();
        return clash;
      }
    }
    if (!options.touching) {
      return std::nullopt;
    }
    clash.kind = ClashKind::Touching;
    clash.witness = contactPoints(distance);
    return clash;
  } catch (const Standard_Failure&) {
    clash.kind = ClashKind::Failed;
    clash.witness.Nullify();
    return clash;
  }
}

ClashResult findClashes(const AssemblyIndex& index, const std::vector<NodeId>& nodes,
                        const ClashOptions& options, const ProgressCallback& progress,
                        const CancelToken* cancel) {
  ClashResult result;
  std::vector<NodeId> parts;
  for (const NodeId node : nodes) {
    if (node >= 0 && static_cast<size_t>(node) < index.nodes().size()) {
      const std::vector<NodeId> below = index.partsBelow(node);
      parts.insert(parts.end(), below.begin(), below.end());
    }
  }
  std::sort(parts.begin(), parts.end());
  parts.erase(std::unique(parts.begin(), parts.end()), parts.end());
  result.partsChecked = static_cast<int>(parts.size());

  // One exact box per part definition, moved to each instance and enlarged so that the pass
  // keeps every pair closer than the clearance.
  const double margin = (options.clearance ? *options.clearance / 2.0 : 0.0) + 0.01;
  std::map<std::string, Bnd_Box> definitionBoxes;
  std::vector<Bnd_Box> boxes(parts.size());
  for (size_t i = 0; i < parts.size(); ++i) {
    if (cancel != nullptr && cancel->isCancelled()) {
      result.cancelled = true;
      return result;
    }
    const auto [it, inserted] = definitionBoxes.try_emplace(index.prototypeId(parts[i]));
    if (inserted) {
      const TopoDS_Shape shape = index.prototypeShape(parts[i]);
      if (!shape.IsNull()) {
        BRepBndLib::AddOptimal(shape, it->second, false, false);
      }
    }
    if (!it->second.IsVoid()) {
      boxes[i] = it->second.Transformed(index.node(parts[i]).location.Transformation());
      boxes[i].Enlarge(margin);
    }
  }
  const std::vector<std::pair<int, int>> pairs = candidatePairs(boxes);
  result.pairsChecked = static_cast<int>(pairs.size());

  // World shapes for the parts in a pair, made here so the parallel checks only read.
  std::vector<TopoDS_Shape> worldShapes(parts.size());
  for (const auto& [i, j] : pairs) {
    for (const int n : {i, j}) {
      if (worldShapes[static_cast<size_t>(n)].IsNull()) {
        worldShapes[static_cast<size_t>(n)] = index.worldShape(parts[static_cast<size_t>(n)]);
      }
    }
  }

  // Pairs are independent and both algorithms only read the shapes (the Boolean runs
  // non-destructively), so they are checked in parallel. Progress is reported in order of
  // completion, one caller at a time.
  std::vector<std::optional<Clash>> found(pairs.size());
  std::atomic<int> done{0};
  std::mutex progressMutex;
  const auto checkOne = [&](int k) {
    if (cancel != nullptr && cancel->isCancelled()) {
      return;
    }
    const auto [i, j] = pairs[static_cast<size_t>(k)];
    found[static_cast<size_t>(k)] =
        checkPair(worldShapes[static_cast<size_t>(i)], worldShapes[static_cast<size_t>(j)], options);
    const int finished = ++done;
    if (progress) {
      const std::lock_guard<std::mutex> lock(progressMutex);
      progress(static_cast<double>(finished) / static_cast<double>(pairs.size()), "Checking clashes");
    }
  };
  OSD_Parallel::For(0, static_cast<int>(pairs.size()), checkOne);
  if (cancel != nullptr && cancel->isCancelled()) {
    result.cancelled = true;
    return result;
  }
  for (size_t k = 0; k < pairs.size(); ++k) {
    if (found[k]) {
      found[k]->a = parts[static_cast<size_t>(pairs[k].first)];
      found[k]->b = parts[static_cast<size_t>(pairs[k].second)];
      result.clashes.push_back(std::move(*found[k]));
    }
  }
  if (progress) {
    progress(1.0, "Checking clashes");
  }

  std::stable_sort(result.clashes.begin(), result.clashes.end(), [](const Clash& a, const Clash& b) {
    if (a.kind != b.kind) {
      return rank(a.kind) < rank(b.kind);
    }
    if (a.kind == ClashKind::Interference) {
      return a.volume > b.volume;
    }
    return a.distance < b.distance;
  });
  result.clashCount = static_cast<int>(result.clashes.size());
  for (const Clash& clash : result.clashes) {
    switch (clash.kind) {
    case ClashKind::Interference:
      ++result.interferences;
      break;
    case ClashKind::Touching:
      ++result.touching;
      break;
    case ClashKind::Clearance:
      ++result.clearances;
      break;
    case ClashKind::Failed:
      ++result.failed;
      break;
    }
  }
  if (result.clashes.size() > static_cast<size_t>(MaxClashRows)) {
    result.clashes.resize(static_cast<size_t>(MaxClashRows));
  }
  return result;
}

} // namespace sc::core
