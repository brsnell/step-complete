#include "render/SceneManager.h"

#include <AIS_ConnectedInteractive.hxx>
#include <AIS_DisplayMode.hxx>
#include <AIS_Shape.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <TCollection_AsciiString.hxx>
#include <TDF_Tool.hxx>
#include <XCAFPrs_AISObject.hxx>
#include <XCAFPrs_Style.hxx>

#include <algorithm>
#include <cstdio>

namespace sc::render {

namespace {

const Quantity_ColorRGBA kDefaultPartColor(Quantity_Color(0.72, 0.75, 0.80, Quantity_TOC_sRGB), 1.0f);
const Quantity_Color kEdgeColor(0.15, 0.16, 0.18, Quantity_TOC_sRGB);

// Part presentation whose fallback color is the color inherited from the
// assembly structure (or a neutral gray), instead of XCAFPrs's white.
class PartPresentation : public XCAFPrs_AISObject {
public:
  PartPresentation(const TDF_Label& label, const Quantity_ColorRGBA& fallback)
      : XCAFPrs_AISObject(label), m_fallback(fallback) {}

  void DefaultStyle(XCAFPrs_Style& style) const override {
    style.SetColorSurf(m_fallback);
    style.SetColorCurv(m_fallback.GetRGB());
  }

private:
  Quantity_ColorRGBA m_fallback;
};

int aisMode(DisplayStyle style) {
  return style == DisplayStyle::Wireframe ? AIS_WireFrame : AIS_Shaded;
}

void configureDrawer(const OccHandle<Prs3d_Drawer>& drawer, DisplayStyle style) {
  const bool edges = style == DisplayStyle::ShadedWithEdges;
  drawer->SetFaceBoundaryDraw(edges);
  if (edges) {
    drawer->SetFaceBoundaryAspect(new Prs3d_LineAspect(kEdgeColor, Aspect_TOL_SOLID, 1.0));
    // Only sharp (C0) edges: skipping tangent edges between fillets and faces keeps
    // filleted parts readable and draws far fewer lines on large models.
    drawer->SetFaceBoundaryUpperContinuity(GeomAbs_C0);
  }
}

std::string prototypeKey(const core::AssemblyNode& node) {
  TCollection_AsciiString entry;
  TDF_Tool::Entry(node.prototypeLabel, entry);
  std::string key = entry.ToCString();
  if (node.inheritedColor) {
    const Quantity_Color& rgb = node.inheritedColor->GetRGB();
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "|%.4f,%.4f,%.4f,%.4f", rgb.Red(), rgb.Green(), rgb.Blue(),
                  static_cast<double>(node.inheritedColor->Alpha()));
    key += buffer;
  }
  return key;
}

} // namespace

SceneManager::SceneManager(const OccHandle<AIS_InteractiveContext>& context) : m_context(context) {
  applyHighlightMode();
}

SceneManager::~SceneManager() {
  clear();
}

void SceneManager::clear() {
  for (const PartEntry& part : m_parts) {
    if (!part.object.IsNull()) {
      m_context->Remove(part.object, false);
    }
  }
  m_parts.clear();
  m_objectToPart.clear();
  m_prototypes.clear();
  m_index.reset();
}

void SceneManager::setModel(std::shared_ptr<const core::AssemblyIndex> index) {
  clear();
  m_index = std::move(index);
  if (!m_index) {
    return;
  }
  m_parts.resize(m_index->nodes().size());
  for (const core::NodeId id : m_index->partInstances()) {
    const core::AssemblyNode& node = m_index->node(id);
    PartEntry& part = m_parts[static_cast<size_t>(id)];
    part.object = createInstance(node);
    if (part.object.IsNull()) {
      continue;
    }
    m_objectToPart[part.object.get()] = id;
    part.visible = node.visibleInFile;
    if (part.visible) {
      displayPart(part.object);
    }
  }
}

OccHandle<XCAFPrs_AISObject> SceneManager::prototypeFor(const core::AssemblyNode& node) {
  const std::string key = prototypeKey(node);
  auto it = m_prototypes.find(key);
  if (it != m_prototypes.end()) {
    return it->second;
  }
  OccHandle<XCAFPrs_AISObject> prototype =
      new PartPresentation(node.prototypeLabel, node.inheritedColor.value_or(kDefaultPartColor));
  applyStyleTo(prototype);
  m_prototypes.emplace(key, prototype);
  return prototype;
}

OccHandle<AIS_InteractiveObject> SceneManager::createInstance(const core::AssemblyNode& node) {
  OccHandle<XCAFPrs_AISObject> prototype = prototypeFor(node);
  OccHandle<AIS_ConnectedInteractive> instance = new AIS_ConnectedInteractive();
  instance->Connect(prototype, node.location.Transformation());
  instance->SetDisplayMode(aisMode(m_style));
  return instance;
}

void SceneManager::detach(core::NodeId id) {
  PartEntry* part = entry(id);
  if (part == nullptr || part->detached || part->object.IsNull()) {
    return;
  }
  const core::AssemblyNode& node = m_index->node(id);
  const bool wasSelected = m_context->IsSelected(part->object);

  OccHandle<XCAFPrs_AISObject> own =
      new PartPresentation(node.prototypeLabel, node.inheritedColor.value_or(kDefaultPartColor));
  own->SetLocalTransformation(node.location.Transformation());
  applyStyleTo(own);
  own->SetDisplayMode(aisMode(m_style));

  m_objectToPart.erase(part->object.get());
  m_context->Remove(part->object, false);
  part->object = own;
  part->detached = true;
  m_objectToPart[own.get()] = id;
  if (part->visible) {
    displayPart(own);
    if (wasSelected) {
      m_context->AddOrRemoveSelected(own, false);
    }
  }
}

void SceneManager::applyStyleTo(const OccHandle<AIS_InteractiveObject>& object) {
  configureDrawer(object->Attributes(), m_style);
}

void SceneManager::applyHighlightMode() {
  // The context's highlight styles default to display mode 0 (wireframe), which
  // on shaded parts only recolors edges. Highlight in the displayed mode so the
  // faces light up. (Per-object SetHilightMode() is avoided on purpose: it
  // creates per-object highlight drawers with hard-coded colors.)
  for (const Prs3d_TypeOfHighlight type :
       {Prs3d_TypeOfHighlight_Selected, Prs3d_TypeOfHighlight_Dynamic, Prs3d_TypeOfHighlight_LocalSelected,
        Prs3d_TypeOfHighlight_LocalDynamic}) {
    m_context->HighlightStyle(type)->SetDisplayMode(aisMode(m_style));
  }
}

void SceneManager::setDisplayStyle(DisplayStyle style) {
  m_style = style;
  applyHighlightMode();
  for (auto& [key, prototype] : m_prototypes) {
    applyStyleTo(prototype);
    prototype->SetToUpdate(); // connected instances recompute from it
  }
  for (const PartEntry& part : m_parts) {
    if (part.object.IsNull()) {
      continue;
    }
    if (part.detached) {
      applyStyleTo(part.object);
    }
    m_context->SetDisplayMode(part.object, aisMode(style), false);
    m_context->Redisplay(part.object, false);
  }
}

std::vector<core::NodeId> SceneManager::expandToParts(const std::vector<core::NodeId>& nodes) const {
  std::vector<core::NodeId> parts;
  if (!m_index) {
    return parts;
  }
  for (const core::NodeId node : nodes) {
    if (node < 0 || static_cast<size_t>(node) >= m_index->nodes().size()) {
      continue;
    }
    const auto below = m_index->partsBelow(node);
    parts.insert(parts.end(), below.begin(), below.end());
  }
  std::sort(parts.begin(), parts.end());
  parts.erase(std::unique(parts.begin(), parts.end()), parts.end());
  return parts;
}

SceneManager::PartEntry* SceneManager::entry(core::NodeId part) {
  if (part < 0 || static_cast<size_t>(part) >= m_parts.size() ||
      m_parts[static_cast<size_t>(part)].object.IsNull()) {
    return nullptr;
  }
  return &m_parts[static_cast<size_t>(part)];
}

const SceneManager::PartEntry* SceneManager::entry(core::NodeId part) const {
  return const_cast<SceneManager*>(this)->entry(part);
}

void SceneManager::setVisible(const std::vector<core::NodeId>& nodes, bool visible) {
  for (const core::NodeId id : expandToParts(nodes)) {
    PartEntry* part = entry(id);
    if (part == nullptr || part->visible == visible) {
      continue;
    }
    part->visible = visible;
    if (visible) {
      displayPart(part->object);
    } else {
      m_context->Erase(part->object, false);
    }
  }
}

void SceneManager::isolate(const std::vector<core::NodeId>& nodes) {
  if (!m_index) {
    return;
  }
  const std::vector<core::NodeId> keep = expandToParts(nodes);
  std::vector<core::NodeId> hide;
  for (const core::NodeId id : m_index->partInstances()) {
    if (!std::binary_search(keep.begin(), keep.end(), id)) {
      hide.push_back(id);
    }
  }
  setVisible(hide, false);
  setVisible(keep, true);
}

void SceneManager::showAll() {
  if (m_index) {
    setVisible(m_index->roots(), true);
  }
}

bool SceneManager::isVisible(core::NodeId node) const {
  for (const core::NodeId id : expandToParts({node})) {
    const PartEntry* part = entry(id);
    if (part != nullptr && part->visible) {
      return true;
    }
  }
  return false;
}

void SceneManager::setTransparency(const std::vector<core::NodeId>& nodes, double transparency) {
  transparency = std::clamp(transparency, 0.0, 1.0);
  for (const core::NodeId id : expandToParts(nodes)) {
    if (transparency > 0.0) {
      detach(id); // connected instances share their prototype's aspects
    }
    PartEntry* part = entry(id);
    if (part == nullptr) {
      continue;
    }
    if (transparency > 0.0) {
      m_context->SetTransparency(part->object, transparency, false);
    } else if (part->detached) {
      m_context->UnsetTransparency(part->object, false);
    }
  }
}

core::NodeId SceneManager::partOf(const OccHandle<AIS_InteractiveObject>& object) const {
  if (object.IsNull()) {
    return core::kNoNode;
  }
  const auto it = m_objectToPart.find(object.get());
  return it == m_objectToPart.end() ? core::kNoNode : it->second;
}

void SceneManager::setClipPlanes(const OccHandle<Graphic3d_SequenceOfHClipPlane>& planes) {
  m_clipPlanes = planes;
  for (const PartEntry& part : m_parts) {
    if (!part.object.IsNull()) {
      part.object->SetClipPlanes(m_clipPlanes);
    }
  }
}

void SceneManager::displayPart(const OccHandle<AIS_InteractiveObject>& object) {
  object->SetClipPlanes(m_clipPlanes);
  // Display with the first active selection mode (which also loads the object
  // into the selector; per-object Activate() does nothing for objects that are
  // not loaded), then activate the remaining modes the current tool wants.
  const std::vector<int> modes = activeSelectionModes();
  m_context->Display(object, aisMode(m_style), modes.front(), false);
  for (size_t i = 1; i < modes.size(); ++i) {
    m_context->Activate(object, modes[i]);
  }
}

std::vector<int> SceneManager::activeSelectionModes() const {
  return m_selectionModes.empty() ? std::vector<int>{0} : m_selectionModes;
}

void SceneManager::setSelectionModes(const std::vector<int>& modes) {
  if (modes == m_selectionModes) {
    return;
  }
  m_context->ClearSelected(false);
  m_selectionModes = modes;
  const std::vector<int> active = activeSelectionModes();
  for (const PartEntry& part : m_parts) {
    if (part.object.IsNull() || !part.visible) {
      continue;
    }
    m_context->Deactivate(part.object);
    for (const int mode : active) {
      m_context->Activate(part.object, mode);
    }
  }
}

std::vector<int> SceneManager::subShapeModes(bool vertices, bool edges, bool faces) {
  std::vector<int> modes;
  if (vertices) {
    modes.push_back(AIS_Shape::SelectionMode(TopAbs_VERTEX));
  }
  if (edges) {
    modes.push_back(AIS_Shape::SelectionMode(TopAbs_EDGE));
  }
  if (faces) {
    modes.push_back(AIS_Shape::SelectionMode(TopAbs_FACE));
  }
  return modes;
}

std::optional<SceneManager::PickedEntity>
SceneManager::pickedEntity(const OccHandle<SelectMgr_EntityOwner>& owner) const {
  const OccHandle<StdSelect_BRepOwner> brepOwner = OccHandle<StdSelect_BRepOwner>::DownCast(owner);
  if (brepOwner.IsNull() || !brepOwner->HasShape() || !m_index) {
    return std::nullopt;
  }
  const core::NodeId part = partOf(OccHandle<AIS_InteractiveObject>::DownCast(brepOwner->Selectable()));
  if (part == core::kNoNode) {
    return std::nullopt;
  }
  // Owners of connected and detached instances hold the prototype-space shape;
  // the instance placement is the node's accumulated location.
  return PickedEntity{part, brepOwner->Shape().Moved(m_index->node(part).location)};
}

std::vector<core::NodeId> SceneManager::selectedParts() const {
  std::vector<core::NodeId> parts;
  for (m_context->InitSelected(); m_context->MoreSelected(); m_context->NextSelected()) {
    const core::NodeId id = partOf(m_context->SelectedInteractive());
    if (id != core::kNoNode && std::find(parts.begin(), parts.end(), id) == parts.end()) {
      parts.push_back(id);
    }
  }
  return parts;
}

void SceneManager::select(const std::vector<core::NodeId>& nodes) {
  m_context->ClearSelected(false);
  for (const core::NodeId id : expandToParts(nodes)) {
    const PartEntry* part = entry(id);
    if (part != nullptr && part->visible && !m_context->IsSelected(part->object)) {
      m_context->AddOrRemoveSelected(part->object, false);
    }
  }
}

Bnd_Box SceneManager::boundingBox(const std::vector<core::NodeId>& nodes) const {
  Bnd_Box total;
  for (const core::NodeId id : expandToParts(nodes)) {
    const PartEntry* part = entry(id);
    if (part != nullptr && part->visible) {
      Bnd_Box box;
      part->object->BoundingBox(box);
      total.Add(box);
    }
  }
  return total;
}

int SceneManager::detachedCount() const {
  return static_cast<int>(
      std::count_if(m_parts.begin(), m_parts.end(), [](const PartEntry& part) { return part.detached; }));
}

} // namespace sc::render
