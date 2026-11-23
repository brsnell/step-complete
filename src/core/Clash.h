#pragma once

#include "core/AssemblyIndex.h"
#include "core/StepImporter.h"

#include <Bnd_Box.hxx>
#include <TopoDS_Shape.hxx>

#include <optional>
#include <utility>
#include <vector>

namespace sc::core {

// Measure > Check Clashes (docs/designs/clash-detection.md).

enum class ClashKind {
  Interference, // the solids share a volume
  Touching,     // in contact, no shared volume
  Clearance,    // closer than the clearance, not touching
  Failed,       // the check could not be done for this pair
};

const char* toString(ClashKind kind);

struct ClashOptions {
  bool touching = false;           // also report pairs in contact
  std::optional<double> clearance; // mm: also report pairs closer than this
};

struct Clash {
  NodeId a = NoNode;
  NodeId b = NoNode;
  ClashKind kind = ClashKind::Interference;
  double volume = 0.0;   // mm³, for an interference
  double distance = 0.0; // mm, for a clearance problem
  TopoDS_Shape witness;  // world coordinates: the shared solid, the gap line or contact points
};

struct ClashResult {
  std::vector<Clash> clashes; // sorted: kind, then largest overlap / smallest gap first
  int partsChecked = 0;       // part instances
  int pairsChecked = 0;       // candidate pairs from the bounding-box pass
  int clashCount = 0;         // all clashes found, including those beyond MaxClashRows
  int interferences = 0;      // per kind, also counting those beyond MaxClashRows
  int touching = 0;
  int clearances = 0;
  int failed = 0;
  bool cancelled = false;
};

inline constexpr double TouchTolerance = 0.001;   // mm
inline constexpr double OverlapTolerance = 0.001; // mm³
inline constexpr int MaxClashRows = 2000;

// Checks the given part instances (assemblies expanded) against each other. Blocking and
// read-only; run it off the UI thread.
ClashResult findClashes(const AssemblyIndex& index, const std::vector<NodeId>& nodes,
                        const ClashOptions& options, const ProgressCallback& progress = {},
                        const CancelToken* cancel = nullptr);

// The broad phase: index pairs (i < j) of boxes that overlap, by sort-and-sweep. Void boxes never pair.
std::vector<std::pair<int, int>> candidatePairs(const std::vector<Bnd_Box>& boxes);

// The exact check of two world-space shapes; nullopt when they are not a clash under the options.
// The result has no part ids.
std::optional<Clash> checkPair(const TopoDS_Shape& a, const TopoDS_Shape& b, const ClashOptions& options);

} // namespace sc::core
