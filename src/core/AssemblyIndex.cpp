#include "core/AssemblyIndex.h"

#include "core/OccCompat.h"

#include <TDF_Tool.hxx>

#include <TDataStd_Name.hxx>
#include <TDataStd_NamedData.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_LayerTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <algorithm>
#include <set>

namespace sc::core {

namespace {

std::string labelName(const TDF_Label& label) {
  OccHandle<TDataStd_Name> name;
  if (!label.IsNull() && label.FindAttribute(TDataStd_Name::GetID(), name)) {
    return toUtf8(name->Get());
  }
  return {};
}

std::optional<Quantity_ColorRGBA> ownColor(const OccHandle<XCAFDoc_ColorTool>& colors,
                                           const TDF_Label& label) {
  Quantity_ColorRGBA color;
  if (colors->GetColor(label, XCAFDoc_ColorSurf, color) || colors->GetColor(label, XCAFDoc_ColorGen, color)) {
    return color;
  }
  return std::nullopt;
}

void appendLayers(const OccHandle<XCAFDoc_LayerTool>& layerTool, const TDF_Label& label,
                  std::vector<std::string>& out) {
  OccHandle<occ::HExtendedStringSequence> names = new occ::HExtendedStringSequence();
  if (!layerTool->GetLayers(label, names)) {
    return;
  }
  for (int i = 1; i <= names->Length(); ++i) {
    std::string layer = toUtf8(names->Value(i));
    if (std::find(out.begin(), out.end(), layer) == out.end()) {
      out.push_back(std::move(layer));
    }
  }
}

// Iterates any NCollection map type without naming it (typedef names differ between OCCT 7 and 8).
template <class Map, class Fn>
void forEachEntry(const Map& map, Fn&& fn) {
  for (typename Map::Iterator it(map); it.More(); it.Next()) {
    fn(it.Key(), it.Value());
  }
}

void appendAttributes(const TDF_Label& label, std::vector<std::pair<std::string, std::string>>& out) {
  OccHandle<TDataStd_NamedData> data;
  if (label.IsNull() || !label.FindAttribute(TDataStd_NamedData::GetID(), data)) {
    return;
  }
  data->LoadDeferredData();
  const size_t first = out.size();
  forEachEntry(data->GetStringsContainer(),
               [&out](const TCollection_ExtendedString& key, const TCollection_ExtendedString& value) {
                 out.emplace_back(toUtf8(key), toUtf8(value));
               });
  forEachEntry(data->GetIntegersContainer(), [&out](const TCollection_ExtendedString& key, int value) {
    out.emplace_back(toUtf8(key), std::to_string(value));
  });
  forEachEntry(data->GetRealsContainer(), [&out](const TCollection_ExtendedString& key, double value) {
    out.emplace_back(toUtf8(key), formatNumber(value, 6));
  });
  // Hash-map order is arbitrary; sort this label's entries by key for a stable display.
  std::sort(out.begin() + static_cast<std::ptrdiff_t>(first), out.end());
}

} // namespace

AssemblyIndex::AssemblyIndex(const DocumentPtr& document) : m_document(document) {
  if (!document || document->ocaf().IsNull()) {
    return;
  }
  const TDF_Label main = document->ocaf()->Main();
  OccHandle<XCAFDoc_ShapeTool> shapeTool = XCAFDoc_DocumentTool::ShapeTool(main);

  occ::LabelSequence freeShapes;
  shapeTool->GetFreeShapes(freeShapes);
  for (const TDF_Label& root : freeShapes) {
    addNode(root, kNoNode, TopLoc_Location(), std::nullopt, true, 0);
  }

  std::set<std::string> prototypes;
  for (const NodeId id : m_partInstances) {
    TCollection_AsciiString entry;
    TDF_Tool::Entry(m_nodes[static_cast<size_t>(id)].prototypeLabel, entry);
    prototypes.insert(entry.ToCString());
  }
  m_uniquePartCount = static_cast<int>(prototypes.size());
}

void AssemblyIndex::addNode(const TDF_Label& label, NodeId parent, const TopLoc_Location& parentLocation,
                            const std::optional<Quantity_ColorRGBA>& parentColor, bool parentVisible,
                            int depth) {
  OccHandle<XCAFDoc_ColorTool> colors = XCAFDoc_DocumentTool::ColorTool(label);

  AssemblyNode node;
  node.id = static_cast<NodeId>(m_nodes.size());
  node.parent = parent;
  node.depth = depth;
  node.instanceLabel = label;
  node.prototypeLabel = label;
  node.location = parentLocation;

  if (XCAFDoc_ShapeTool::IsReference(label)) {
    TDF_Label referred;
    if (XCAFDoc_ShapeTool::GetReferredShape(label, referred)) {
      node.prototypeLabel = referred;
    }
    node.location = parentLocation * XCAFDoc_ShapeTool::GetLocation(label);
  }

  node.kind = XCAFDoc_ShapeTool::IsAssembly(node.prototypeLabel) ? NodeKind::Assembly : NodeKind::Part;
  node.prototypeName = labelName(node.prototypeLabel);
  node.name = labelName(node.instanceLabel);
  // XCAF auto-names component instances "=>[0:1:1:2]"; those mean nothing to users.
  if (node.name.empty() || node.name.rfind("=>", 0) == 0) {
    node.name = node.prototypeName;
  }
  if (node.name.empty()) {
    node.name = node.kind == NodeKind::Assembly ? "Unnamed assembly" : "Unnamed part";
  }

  // Most specific wins: this instance's color, else whatever the parent passes down.
  const std::optional<Quantity_ColorRGBA> instanceColor = ownColor(colors, node.instanceLabel);
  node.inheritedColor = instanceColor ? instanceColor : parentColor;
  node.visibleInFile =
      parentVisible && colors->IsVisible(node.instanceLabel) && colors->IsVisible(node.prototypeLabel);

  const NodeId id = node.id;
  const NodeKind kind = node.kind;
  const TDF_Label prototype = node.prototypeLabel;
  const TopLoc_Location location = node.location;
  const bool visible = node.visibleInFile;

  m_nodes.push_back(std::move(node));
  if (parent == kNoNode) {
    m_roots.push_back(id);
  } else {
    m_nodes[static_cast<size_t>(parent)].children.push_back(id);
  }

  if (kind == NodeKind::Part) {
    m_partInstances.push_back(id);
    return;
  }

  // Children inherit: this assembly instance's color, else the assembly definition's, else ours.
  std::optional<Quantity_ColorRGBA> childColor = instanceColor;
  if (!childColor) {
    childColor = ownColor(colors, prototype);
  }
  if (!childColor) {
    childColor = parentColor;
  }
  occ::LabelSequence components;
  XCAFDoc_ShapeTool::GetComponents(prototype, components, false);
  for (const TDF_Label& component : components) {
    addNode(component, id, location, childColor, visible, depth + 1);
  }
}

std::vector<NodeId> AssemblyIndex::subtree(NodeId id) const {
  std::vector<NodeId> result;
  std::vector<NodeId> stack{id};
  while (!stack.empty()) {
    const NodeId current = stack.back();
    stack.pop_back();
    result.push_back(current);
    const auto& children = node(current).children;
    stack.insert(stack.end(), children.rbegin(), children.rend());
  }
  return result;
}

std::vector<NodeId> AssemblyIndex::partsBelow(NodeId id) const {
  std::vector<NodeId> result;
  for (const NodeId n : subtree(id)) {
    if (node(n).kind == NodeKind::Part) {
      result.push_back(n);
    }
  }
  return result;
}

int AssemblyIndex::instanceCount(NodeId id) const {
  const TDF_Label& prototype = node(id).prototypeLabel;
  return static_cast<int>(std::count_if(m_nodes.begin(), m_nodes.end(), [&prototype](const AssemblyNode& n) {
    return n.prototypeLabel == prototype;
  }));
}

std::string AssemblyIndex::displayPath(NodeId id) const {
  std::vector<const std::string*> names;
  for (NodeId current = id; current != kNoNode; current = node(current).parent) {
    names.push_back(&node(current).name);
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

TopoDS_Shape AssemblyIndex::prototypeShape(NodeId id) const {
  return XCAFDoc_ShapeTool::GetShape(node(id).prototypeLabel);
}

TopoDS_Shape AssemblyIndex::worldShape(NodeId id) const {
  TopoDS_Shape shape = prototypeShape(id);
  if (!shape.IsNull()) {
    shape.Location(node(id).location * shape.Location());
  }
  return shape;
}

std::vector<std::pair<std::string, std::string>> AssemblyIndex::attributes(NodeId id) const {
  std::vector<std::pair<std::string, std::string>> result;
  const AssemblyNode& n = node(id);
  appendAttributes(n.instanceLabel, result);
  if (!(n.prototypeLabel == n.instanceLabel)) {
    appendAttributes(n.prototypeLabel, result);
  }
  return result;
}

std::vector<std::string> AssemblyIndex::layers(NodeId id) const {
  std::vector<std::string> result;
  const AssemblyNode& n = node(id);
  OccHandle<XCAFDoc_LayerTool> layerTool = XCAFDoc_DocumentTool::LayerTool(n.instanceLabel);
  appendLayers(layerTool, n.instanceLabel, result);
  appendLayers(layerTool, n.prototypeLabel, result);
  return result;
}

} // namespace sc::core
