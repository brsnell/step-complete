#pragma once

#include "core/AssemblyIndex.h"

#include <gp_Vec.hxx>

#include <vector>

namespace sc::core {

// View > Exploded View (docs/designs/exploded-view.md): where each node of an assembly moves.
//
// By assembly level: a node moves away from its parent's center by the distance between the
// two centers (bounding-box centers, in world coordinates), times the distance factor, on top
// of its parent's own move. A sub-assembly so moves as a unit while its parts spread around
// it. Top-level nodes move away from the center of the whole model.
//
// The offsets are linear in the factor: the directions are computed once (one bounding box per
// part definition, moved to each instance) and offset() only scales them.
class ExplodeLayout {
public:
  ExplodeLayout() = default;
  explicit ExplodeLayout(const AssemblyIndex& index);

  // At least one part moves: false for an empty model, a single part, or parts that all share
  // their assembly's center.
  bool canExplode() const { return _canExplode; }

  // The world translation of a node at this factor (0: assembled; 1: every node twice as far
  // from its parent's center). Zero for an unknown node.
  gp_Vec offset(NodeId node, double factor) const;
  // Every node's offset at this factor, by NodeId.
  std::vector<gp_Vec> offsets(double factor) const;

private:
  std::vector<gp_Vec> _directions; // by NodeId, at factor 1
  bool _canExplode = false;
};

} // namespace sc::core
