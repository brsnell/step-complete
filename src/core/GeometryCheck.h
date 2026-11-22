#pragma once

#include "core/AssemblyIndex.h"
#include "core/StepImporter.h"

#include <BRepCheck_Status.hxx>
#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

namespace sc::core {

// Measure > Check Geometry (docs/designs/geometry-check.md).

enum class IssueKind {
  Invalid,   // BRepCheck_Analyzer found a status other than NoError
  OpenShell, // a shell of a solid with free edges: the solid has a gap
  TinyEdge,  // an edge shorter than TinyEdgeLength
};

const char* toString(IssueKind kind);

struct GeometryIssue {
  NodeId part = NoNode; // the instance that was checked (its placement is used for the highlight)
  int instances = 1;    // instances sharing the part definition, so the problem
  IssueKind kind = IssueKind::Invalid;
  std::string shapeType; // "Face", "Edge", ...
  std::string detail;    // e.g. "Self-intersecting wire", "4 free edges", "0.0004 mm long"
  TopoDS_Shape shape;    // the faulty sub-shape in the part's own coordinates
};

struct GeometryCheckResult {
  std::vector<GeometryIssue> issues;
  int partsChecked = 0; // part definitions
  int partsWithIssues = 0;
  int issueCount = 0; // all issues found, including those beyond the per-part cap
  bool cancelled = false;
};

inline constexpr double TinyEdgeLength = 0.001; // mm
inline constexpr int MaxIssuesPerPart = 200;

// Checks the given part instances (assemblies expanded), once per definition: the first given
// instance of each is the one reported. Blocking and read-only; run it off the UI thread.
GeometryCheckResult checkGeometry(const AssemblyIndex& index, const std::vector<NodeId>& nodes,
                                  const ProgressCallback& progress = {}, const CancelToken* cancel = nullptr);

// Checks one shape (exposed for tests): the issues have no part.
std::vector<GeometryIssue> checkShape(const TopoDS_Shape& shape, int* totalFound = nullptr);

// OCCT's status in plain words: BRepCheck_SelfIntersectingWire -> "Self-intersecting wire".
std::string statusText(BRepCheck_Status status);

} // namespace sc::core
