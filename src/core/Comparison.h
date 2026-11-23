#pragma once

#include "core/AssemblyIndex.h"
#include "core/OccHandle.h"
#include "core/StepImporter.h"

#include <Poly_Triangulation.hxx>

#include <string>
#include <utility>
#include <vector>

namespace sc::core {

// File > Compare With (docs/designs/model-comparison.md): the open model (current revision)
// against another revision of it, part by part.

enum class ChangeKind {
  Unchanged,
  Moved,   // the same geometry in another place
  Changed, // different geometry
  Added,   // only in the current revision
  Removed, // only in the other revision
};

const char* toString(ChangeKind kind);

struct PartChange {
  ChangeKind kind = ChangeKind::Unchanged;
  NodeId current = NoNode; // the part in the current revision (NoNode when removed)
  NodeId other = NoNode;   // its partner in the other revision (NoNode when added)
  std::string path;        // below the root: "Pin pair / Pin Ø6"
  // Changed parts: the largest distance between the two meshes, either way (mm), and the
  // current part's mesh (own coordinates) with a signed deviation per node (node i at [i - 1]):
  // positive where the current revision has material the other has not.
  double maxDeviation = 0.0;
  OccHandle<Poly_Triangulation> mesh;
  std::vector<float> deviation;
};

struct ComparisonOptions {
  double tolerance = 0.01; // mm: positions within it are equal, deviations within it are green
};

struct ComparisonResult {
  std::vector<PartChange> changes; // the current revision's parts in tree order, then the removed ones
  int unchanged = 0;
  int moved = 0;
  int changed = 0;
  int added = 0;
  int removed = 0;
  double maxDeviation = 0.0; // over the changed parts
  bool cancelled = false;
};

// Pairs part instances: by their path below the root (repeated paths in tree order); what is left
// by the same shape (volume, area, moments) nearest in place, so renamed or re-exported parts
// pair; then by part name ignoring case and an instance number ("nut_2" and "NUT"). Parts without
// geometry are left out. Returns (current, other) pairs, NoNode for a part with no partner.
std::vector<std::pair<NodeId, NodeId>> pairParts(const AssemblyIndex& current, const AssemblyIndex& other);

// The path used for pairing: the instance's names below the root, joined by " / ".
std::string pairingPath(const AssemblyIndex& index, NodeId part);

// Compares every part. Blocking and read-only (both models' meshes must exist: imports are
// premeshed); run it off the UI thread.
ComparisonResult compareModels(const AssemblyIndex& current, const AssemblyIndex& other,
                               const ComparisonOptions& options = {}, const ProgressCallback& progress = {},
                               const CancelToken* cancel = nullptr);

} // namespace sc::core
