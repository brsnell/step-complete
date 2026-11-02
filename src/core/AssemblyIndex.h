#pragma once

#include "core/Document.h"

#include <Quantity_ColorRGBA.hxx>
#include <TDF_Label.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sc::core {

using NodeId = std::int32_t;
inline constexpr NodeId kNoNode = -1;

enum class NodeKind {
  Assembly,
  Part,
};

// One node of the flattened product structure: an assembly or part *instance*.
// A part used 20 times appears as 20 nodes sharing one prototypeLabel.
struct AssemblyNode {
  NodeId id = kNoNode;
  NodeId parent = kNoNode;
  std::vector<NodeId> children;
  NodeKind kind = NodeKind::Part;
  int depth = 0;

  std::string name;          // instance name if set, else the product (prototype) name
  std::string prototypeName; // product name of the referenced part/assembly

  TDF_Label instanceLabel;  // component label (or the root's own label for roots)
  TDF_Label prototypeLabel; // the referred part/assembly definition
  TopLoc_Location location; // accumulated world placement of this instance

  // Color this instance inherits from its own instance label or enclosing
  // assemblies; applies to faces whose part defines no color of its own.
  std::optional<Quantity_ColorRGBA> inheritedColor;
  bool visibleInFile = true;
};

// Read-only index over an XCAF document's assembly structure. Everything in
// the UI refers to nodes by NodeId, never to raw labels.
class AssemblyIndex {
public:
  AssemblyIndex() = default;
  explicit AssemblyIndex(const DocumentPtr& document);

  const DocumentPtr& document() const { return m_document; }
  bool isEmpty() const { return m_nodes.empty(); }

  const std::vector<AssemblyNode>& nodes() const { return m_nodes; }
  const AssemblyNode& node(NodeId id) const { return m_nodes.at(static_cast<size_t>(id)); }
  const std::vector<NodeId>& roots() const { return m_roots; }

  // All part instances, in tree order.
  const std::vector<NodeId>& partInstances() const { return m_partInstances; }
  // Number of distinct part definitions (prototypes).
  int uniquePartCount() const { return m_uniquePartCount; }

  // Node and all nodes below it, in tree order.
  std::vector<NodeId> subtree(NodeId id) const;
  // Part instances in the subtree.
  std::vector<NodeId> partsBelow(NodeId id) const;
  // Number of instances in the whole model that share this node's prototype.
  int instanceCount(NodeId id) const;
  // "Top / Subassembly / Part", for tooltips and properties.
  std::string displayPath(NodeId id) const;

  // Prototype shape (no placement) and the shape placed in world coordinates.
  TopoDS_Shape prototypeShape(NodeId id) const;
  TopoDS_Shape worldShape(NodeId id) const;

  // Layers assigned to the instance or its prototype.
  std::vector<std::string> layers(NodeId id) const;

private:
  void addNode(const TDF_Label& label, NodeId parent, const TopLoc_Location& parentLocation,
               const std::optional<Quantity_ColorRGBA>& parentColor, bool parentVisible, int depth);

  DocumentPtr m_document;
  std::vector<AssemblyNode> m_nodes;
  std::vector<NodeId> m_roots;
  std::vector<NodeId> m_partInstances;
  int m_uniquePartCount = 0;
};

} // namespace sc::core
