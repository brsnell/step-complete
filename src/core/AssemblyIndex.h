#pragma once

#include "core/Document.h"

#include <Quantity_ColorRGBA.hxx>
#include <TDF_Label.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
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

// Validation properties (volume, surface area, centroid) that a STEP file declares
// for a product, as stored by OCCT's STEP reader (PropsMode) in the XCAFDoc_Volume,
// XCAFDoc_Area and XCAFDoc_Centroid attributes. They are the file author's claim
// about the geometry, shown next to our own computed mass properties so users can
// verify the model.
struct ValidationProperties {
  std::optional<double> volume;   // mm^3
  std::optional<double> area;     // mm^2
  std::optional<gp_Pnt> centroid; // mm, in the PART's own (prototype) coordinates

  bool empty() const { return !volume && !area && !centroid; }
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

  // User-defined attributes from the STEP file (part number, material, ...),
  // read by OCCT >= 7.7. Instance attributes first, then the product's.
  std::vector<std::pair<std::string, std::string>> attributes(NodeId id) const;

  // Validation properties declared in the file for this node. Each attribute is
  // taken from the instance label if present there, else from the prototype label.
  // Values are in the document unit (mm): OCCT's reader scales them with the file's
  // length unit (verified on 7.6 with an inch-labelled file). The centroid is NOT placed: it is in
  // prototype coordinates, so callers apply node.location for world space.
  ValidationProperties validationProperties(NodeId id) const;

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
