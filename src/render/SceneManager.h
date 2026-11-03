#pragma once

#include "core/AssemblyIndex.h"
#include "core/OccHandle.h"
#include "render/SceneStyle.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <Bnd_Box.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <TopoDS_Shape.hxx>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class XCAFPrs_AISObject;

namespace sc::render {

// Builds and manages the 3D presentation of an assembly.
//
// Instancing: each distinct part (prototype label + inherited color) gets one
// XCAFPrs_AISObject, which is never displayed itself. Every part instance is
// an AIS_ConnectedInteractive that references it with its own placement, so a
// part used 500 times is triangulated and uploaded to the GPU once.
//
// An instance that needs its own appearance (e.g. transparency) is detached
// into a dedicated presentation, because connected objects share their
// prototype's graphic aspects.
//
// All methods take node ids; assembly nodes apply to every part below them.
// Methods never redraw the viewer; the caller invalidates the view afterwards.
class SceneManager {
public:
  explicit SceneManager(const OccHandle<AIS_InteractiveContext>& context);
  ~SceneManager();

  SceneManager(const SceneManager&) = delete;
  SceneManager& operator=(const SceneManager&) = delete;

  void setModel(std::shared_ptr<const core::AssemblyIndex> index);
  void clear();
  const std::shared_ptr<const core::AssemblyIndex>& model() const { return m_index; }

  DisplayStyle displayStyle() const { return m_style; }
  void setDisplayStyle(DisplayStyle style);

  // --- Visibility ---------------------------------------------------------------
  void setVisible(const std::vector<core::NodeId>& nodes, bool visible);
  void isolate(const std::vector<core::NodeId>& nodes);
  void showAll();
  // Parts: whether shown. Assemblies: whether any part below is shown.
  bool isVisible(core::NodeId node) const;

  // --- Appearance -----------------------------------------------------------------
  // 0 = opaque, 1 = invisible.
  void setTransparency(const std::vector<core::NodeId>& nodes, double transparency);

  // --- Selection ------------------------------------------------------------------
  // Part instance shown by an interactive object, or kNoNode (e.g. the view cube).
  core::NodeId partOf(const OccHandle<AIS_InteractiveObject>& object) const;
  // Currently selected part instances, from the context's selection.
  std::vector<core::NodeId> selectedParts() const;
  // Replaces the context selection with the visible parts below the given nodes.
  void select(const std::vector<core::NodeId>& nodes);

  // --- Picking modes -------------------------------------------------------------
  // Selection modes active on every displayed part (AIS_Shape::SelectionMode
  // values). Empty means whole parts (mode 0), the default. Changing modes
  // clears the selection.
  void setSelectionModes(const std::vector<int>& modes);
  const std::vector<int>& selectionModes() const { return m_selectionModes; }
  static std::vector<int> subShapeModes(bool vertices, bool edges, bool faces);

  struct PickedEntity {
    core::NodeId part = core::kNoNode;
    TopoDS_Shape worldShape; // picked vertex/edge/face, placed in world coordinates
  };
  // The B-rep entity behind a sub-shape selection owner, or nullopt (whole
  // objects, the view cube, measurement witnesses).
  std::optional<PickedEntity> pickedEntity(const OccHandle<SelectMgr_EntityOwner>& owner) const;

  // World bounding box of the visible parts below the nodes (void if none).
  Bnd_Box boundingBox(const std::vector<core::NodeId>& nodes) const;

  // Diagnostics: number of shared prototypes and of instances using them.
  int prototypeCount() const { return static_cast<int>(m_prototypes.size()); }
  int detachedCount() const;

private:
  struct PartEntry {
    OccHandle<AIS_InteractiveObject> object; // connected instance or detached presentation
    bool visible = false;
    bool detached = false;
  };

  OccHandle<XCAFPrs_AISObject> prototypeFor(const core::AssemblyNode& node);
  OccHandle<AIS_InteractiveObject> createInstance(const core::AssemblyNode& node);
  void detach(core::NodeId part);
  void displayPart(const OccHandle<AIS_InteractiveObject>& object);
  std::vector<int> activeSelectionModes() const;
  void applyStyleTo(const OccHandle<AIS_InteractiveObject>& object);
  void applyHighlightMode();
  std::vector<core::NodeId> expandToParts(const std::vector<core::NodeId>& nodes) const;
  PartEntry* entry(core::NodeId part);
  const PartEntry* entry(core::NodeId part) const;

  OccHandle<AIS_InteractiveContext> m_context;
  std::shared_ptr<const core::AssemblyIndex> m_index;
  std::vector<PartEntry> m_parts; // indexed by NodeId; empty object for assemblies
  std::unordered_map<const AIS_InteractiveObject*, core::NodeId> m_objectToPart;
  std::map<std::string, OccHandle<XCAFPrs_AISObject>> m_prototypes;
  DisplayStyle m_style = DisplayStyle::ShadedWithEdges;
  std::vector<int> m_selectionModes;
};

} // namespace sc::render
