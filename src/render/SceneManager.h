#pragma once

#include "core/AssemblyIndex.h"
#include "core/OccHandle.h"
#include "render/SceneStyle.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <Bnd_Box.hxx>
#include <Graphic3d_SequenceOfHClipPlane.hxx>
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
  const std::shared_ptr<const core::AssemblyIndex>& model() const { return _index; }

  DisplayStyle displayStyle() const { return _style; }
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
  // Part instance shown by an interactive object, or NoNode (e.g. the view cube).
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
  const std::vector<int>& selectionModes() const { return _selectionModes; }
  static std::vector<int> subShapeModes(bool vertices, bool edges, bool faces);

  struct PickedEntity {
    core::NodeId part = core::NoNode;
    TopoDS_Shape worldShape; // picked vertex/edge/face, placed in world coordinates
  };
  // The B-rep entity behind a sub-shape selection owner, or nullopt (whole
  // objects, the view cube, measurement witnesses).
  std::optional<PickedEntity> pickedEntity(const OccHandle<SelectMgr_EntityOwner>& owner) const;

  // --- Section planes ------------------------------------------------------------
  // Clip planes applied to every part (not to PMI, measurement witnesses or the
  // view cube, which stay whole). Null or empty removes clipping.
  void setClipPlanes(const OccHandle<Graphic3d_SequenceOfHClipPlane>& planes);

  // --- Geometry check -------------------------------------------------------------
  // Draws one faulty face, edge or vertex (world coordinates) in red on top of the
  // model, replacing the previous one; a null shape clears it. Not selectable.
  void showIssue(const TopoDS_Shape& worldShape);
  void clearIssue();
  bool hasIssue() const { return !_issue.IsNull(); }

  // --- Part overrides ----------------------------------------------------------------
  // Shows another presentation in place of a part's own (e.g. the wall thickness colors) while
  // keeping its visibility, selection and clip planes; selecting it selects the part. Null
  // restores the part's own presentation. The override is positioned by its own transformation.
  void setPartOverride(core::NodeId part, const OccHandle<AIS_InteractiveObject>& presentation);
  void clearPartOverrides();
  bool hasOverride(core::NodeId part) const;

  // World bounding box of the visible parts below the nodes (void if none).
  Bnd_Box boundingBox(const std::vector<core::NodeId>& nodes) const;

  // Diagnostics: number of shared prototypes and of instances using them.
  int prototypeCount() const { return static_cast<int>(_prototypes.size()); }
  int detachedCount() const;

private:
  struct PartEntry {
    OccHandle<AIS_InteractiveObject> object; // connected instance or detached presentation
    OccHandle<AIS_InteractiveObject> own;    // while overridden: the part's own presentation
    bool visible = false;
    bool detached = false;
  };

  OccHandle<XCAFPrs_AISObject> prototypeFor(const core::AssemblyNode& node);
  OccHandle<AIS_InteractiveObject> createInstance(const core::AssemblyNode& node);
  void detach(core::NodeId part);
  void replaceObject(core::NodeId id, PartEntry& part, const OccHandle<AIS_InteractiveObject>& object);
  void displayPart(const OccHandle<AIS_InteractiveObject>& object);
  std::vector<int> activeSelectionModes() const;
  void applyStyleTo(const OccHandle<AIS_InteractiveObject>& object);
  void applyHighlightMode();
  std::vector<core::NodeId> expandToParts(const std::vector<core::NodeId>& nodes) const;
  PartEntry* entry(core::NodeId part);
  const PartEntry* entry(core::NodeId part) const;

  OccHandle<AIS_InteractiveContext> _context;
  std::shared_ptr<const core::AssemblyIndex> _index;
  std::vector<PartEntry> _parts; // indexed by NodeId; empty object for assemblies
  std::unordered_map<const AIS_InteractiveObject*, core::NodeId> _objectToPart;
  std::map<std::string, OccHandle<XCAFPrs_AISObject>> _prototypes;
  DisplayStyle _style = DisplayStyle::ShadedWithEdges;
  std::vector<int> _selectionModes;
  OccHandle<Graphic3d_SequenceOfHClipPlane> _clipPlanes;
  OccHandle<AIS_InteractiveObject> _issue; // the geometry check's red highlight
};

} // namespace sc::render
