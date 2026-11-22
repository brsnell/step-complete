#include "core/GeometryCheck.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Result.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <ShapeAnalysis_Shell.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>

#include <cstdio>
#include <map>
#include <set>

namespace sc::core {

namespace {

const char* shapeTypeName(TopAbs_ShapeEnum type) {
  switch (type) {
  case TopAbs_VERTEX:
    return "Vertex";
  case TopAbs_EDGE:
    return "Edge";
  case TopAbs_WIRE:
    return "Wire";
  case TopAbs_FACE:
    return "Face";
  case TopAbs_SHELL:
    return "Shell";
  case TopAbs_SOLID:
    return "Solid";
  case TopAbs_COMPSOLID:
    return "Compound solid";
  case TopAbs_COMPOUND:
    return "Compound";
  case TopAbs_SHAPE:
    break;
  }
  return "Shape";
}

// Collects every distinct non-NoError status of one sub-shape, its own and in context.
template <class List>
void addStatuses(const List& statuses, std::set<BRepCheck_Status>& out) {
  for (const BRepCheck_Status status : statuses) {
    if (status != BRepCheck_NoError) {
      out.insert(status);
    }
  }
}

struct Collector {
  std::vector<GeometryIssue> issues;
  int found = 0;
  void add(IssueKind kind, const TopoDS_Shape& shape, std::string detail) {
    ++found;
    if (static_cast<int>(issues.size()) < MaxIssuesPerPart) {
      GeometryIssue issue;
      issue.kind = kind;
      issue.shapeType = shapeTypeName(shape.ShapeType());
      issue.detail = std::move(detail);
      issue.shape = shape;
      issues.push_back(std::move(issue));
    }
  }
};

void invalidGeometry(const TopoDS_Shape& shape, Collector& out) {
  const BRepCheck_Analyzer analyzer(shape, true);
  if (analyzer.IsValid()) {
    return;
  }
  // Each sub-shape once, from vertices up, so the most specific shape is listed first.
  for (const TopAbs_ShapeEnum type :
       {TopAbs_VERTEX, TopAbs_EDGE, TopAbs_WIRE, TopAbs_FACE, TopAbs_SHELL, TopAbs_SOLID}) {
    TopTools_IndexedMapOfShape subShapes;
    TopExp::MapShapes(shape, type, subShapes);
    for (int i = 1; i <= subShapes.Extent(); ++i) {
      const TopoDS_Shape& sub = subShapes(i);
      const OccHandle<BRepCheck_Result>& result = analyzer.Result(sub);
      if (result.IsNull()) {
        continue;
      }
      std::set<BRepCheck_Status> statuses;
      addStatuses(result->Status(), statuses);
      for (result->InitContextIterator(); result->MoreShapeInContext(); result->NextShapeInContext()) {
        addStatuses(result->StatusOnShape(), statuses);
      }
      for (const BRepCheck_Status status : statuses) {
        out.add(IssueKind::Invalid, sub, statusText(status));
      }
    }
  }
}

void openShells(const TopoDS_Shape& shape, Collector& out) {
  for (TopExp_Explorer solid(shape, TopAbs_SOLID); solid.More(); solid.Next()) {
    for (TopExp_Explorer shell(solid.Current(), TopAbs_SHELL); shell.More(); shell.Next()) {
      ShapeAnalysis_Shell analysis;
      analysis.LoadShells(shell.Current());
      analysis.CheckOrientedShells(shell.Current(), true);
      if (!analysis.HasFreeEdges()) {
        continue;
      }
      const TopoDS_Compound freeEdges = analysis.FreeEdges();
      int count = 0;
      for (TopExp_Explorer edge(freeEdges, TopAbs_EDGE); edge.More(); edge.Next()) {
        ++count;
      }
      out.add(IssueKind::OpenShell, freeEdges,
              std::to_string(count) + (count == 1 ? " free edge" : " free edges") + ": the solid has a gap");
    }
  }
}

void tinyEdges(const TopoDS_Shape& shape, Collector& out) {
  TopTools_IndexedMapOfShape edges;
  TopExp::MapShapes(shape, TopAbs_EDGE, edges);
  for (int i = 1; i <= edges.Extent(); ++i) {
    const TopoDS_Edge& edge = TopoDS::Edge(edges(i));
    if (BRep_Tool::Degenerated(edge) || !BRep_Tool::IsGeometric(edge)) {
      continue;
    }
    double length = 0.0;
    try {
      const BRepAdaptor_Curve curve(edge);
      length = GCPnts_AbscissaPoint::Length(curve);
    } catch (const Standard_Failure&) {
      continue; // invalid edges are reported by the shape check
    }
    if (length < TinyEdgeLength) {
      char text[64];
      std::snprintf(text, sizeof text, "%.6f mm long", length);
      out.add(IssueKind::TinyEdge, edge, text);
    }
  }
}

} // namespace

const char* toString(IssueKind kind) {
  switch (kind) {
  case IssueKind::Invalid:
    return "Invalid geometry";
  case IssueKind::OpenShell:
    return "Open shell";
  case IssueKind::TinyEdge:
    return "Tiny edge";
  }
  return "Problem";
}

std::string statusText(BRepCheck_Status status) {
  switch (status) {
  case BRepCheck_NoError:
    return "No error";
  case BRepCheck_InvalidPointOnCurve:
    return "Vertex not on its edge's curve";
  case BRepCheck_InvalidPointOnCurveOnSurface:
    return "Vertex not on the edge's curve on the face";
  case BRepCheck_InvalidPointOnSurface:
    return "Vertex not on the face's surface";
  case BRepCheck_No3DCurve:
    return "Edge without a 3D curve";
  case BRepCheck_Multiple3DCurve:
    return "Edge with several 3D curves";
  case BRepCheck_Invalid3DCurve:
    return "Invalid 3D curve";
  case BRepCheck_NoCurveOnSurface:
    return "Edge without a curve on the face's surface";
  case BRepCheck_InvalidCurveOnSurface:
    return "Curve on surface does not match the 3D curve";
  case BRepCheck_InvalidCurveOnClosedSurface:
    return "Invalid curve on a closed surface";
  case BRepCheck_InvalidSameRangeFlag:
    return "Invalid same-range flag";
  case BRepCheck_InvalidSameParameterFlag:
    return "Invalid same-parameter flag";
  case BRepCheck_InvalidDegeneratedFlag:
    return "Invalid degenerated flag";
  case BRepCheck_FreeEdge:
    return "Free edge";
  case BRepCheck_InvalidMultiConnexity:
    return "Edge shared by too many faces";
  case BRepCheck_InvalidRange:
    return "Invalid parameter range";
  case BRepCheck_EmptyWire:
    return "Empty wire";
  case BRepCheck_RedundantEdge:
    return "Redundant edge";
  case BRepCheck_SelfIntersectingWire:
    return "Self-intersecting wire";
  case BRepCheck_NoSurface:
    return "Face without a surface";
  case BRepCheck_InvalidWire:
    return "Invalid wire";
  case BRepCheck_RedundantWire:
    return "Redundant wire";
  case BRepCheck_IntersectingWires:
    return "Intersecting wires";
  case BRepCheck_InvalidImbricationOfWires:
    return "Wires nested wrongly";
  case BRepCheck_EmptyShell:
    return "Empty shell";
  case BRepCheck_RedundantFace:
    return "Redundant face";
  case BRepCheck_InvalidImbricationOfShells:
    return "Shells nested wrongly";
  case BRepCheck_UnorientableShape:
    return "Unorientable shape";
  case BRepCheck_NotClosed:
    return "Not closed";
  case BRepCheck_NotConnected:
    return "Not connected";
  case BRepCheck_SubshapeNotInShape:
    return "Sub-shape not in its shape";
  case BRepCheck_BadOrientation:
    return "Bad orientation";
  case BRepCheck_BadOrientationOfSubshape:
    return "Bad orientation of a sub-shape";
  case BRepCheck_InvalidPolygonOnTriangulation:
    return "Invalid polygon on triangulation";
  case BRepCheck_InvalidToleranceValue:
    return "Invalid tolerance value";
  case BRepCheck_EnclosedRegion:
    return "Enclosed region";
  case BRepCheck_CheckFail:
    return "The check itself failed";
  }
  return "Unknown problem";
}

std::vector<GeometryIssue> checkShape(const TopoDS_Shape& shape, int* totalFound) {
  Collector collector;
  if (!shape.IsNull()) {
    try {
      invalidGeometry(shape, collector);
      openShells(shape, collector);
      tinyEdges(shape, collector);
    } catch (const Standard_Failure&) {
      collector.add(IssueKind::Invalid, shape, statusText(BRepCheck_CheckFail));
    }
  }
  if (totalFound != nullptr) {
    *totalFound = collector.found;
  }
  return collector.issues;
}

GeometryCheckResult checkGeometry(const AssemblyIndex& index, const std::vector<NodeId>& nodes,
                                  const ProgressCallback& progress, const CancelToken* cancel) {
  GeometryCheckResult result;
  // The first given instance of each definition, in tree order, and how many instances share it.
  std::vector<char> chosen(index.nodes().size(), 0);
  for (const NodeId node : nodes) {
    if (node >= 0 && static_cast<size_t>(node) < index.nodes().size()) {
      for (const NodeId part : index.partsBelow(node)) {
        chosen[static_cast<size_t>(part)] = 1;
      }
    }
  }
  std::vector<NodeId> firsts;
  std::map<std::string, size_t> slot;
  std::vector<int> instanceCounts;
  for (const NodeId part : index.partInstances()) {
    if (chosen[static_cast<size_t>(part)] == 0) {
      continue;
    }
    const auto [it, inserted] = slot.emplace(index.prototypeId(part), firsts.size());
    if (inserted) {
      firsts.push_back(part);
      instanceCounts.push_back(0);
    }
    ++instanceCounts[it->second];
  }

  for (size_t i = 0; i < firsts.size(); ++i) {
    if (cancel != nullptr && cancel->isCancelled()) {
      result.cancelled = true;
      return result;
    }
    if (progress) {
      progress(static_cast<double>(i) / static_cast<double>(firsts.size()), "Checking geometry");
    }
    int found = 0;
    std::vector<GeometryIssue> issues = checkShape(index.prototypeShape(firsts[i]), &found);
    ++result.partsChecked;
    if (found > 0) {
      ++result.partsWithIssues;
      result.issueCount += found;
    }
    for (GeometryIssue& issue : issues) {
      issue.part = firsts[i];
      issue.instances = instanceCounts[i];
      result.issues.push_back(std::move(issue));
    }
  }
  if (progress) {
    progress(1.0, "Checking geometry");
  }
  return result;
}

} // namespace sc::core
