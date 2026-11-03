#pragma once

#include "core/AssemblyIndex.h"
#include "core/OccHandle.h"
#include "core/PmiModel.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <Graphic3d_ClipPlane.hxx>
#include <V3d_View.hxx>

#include <memory>
#include <unordered_map>
#include <vector>

namespace sc::render {

// Draws one PMI item at one placement: the file's graphical presentation
// (polylines and triangulated text/symbols) when present, otherwise the
// semantic text as a screen-space label with a leader to the geometry it
// applies to. Selectable as a whole (mode 0).
class PmiPresentation : public AIS_InteractiveObject {
  DEFINE_STANDARD_RTTI_INLINE(PmiPresentation, AIS_InteractiveObject)

public:
  PmiPresentation(const core::PmiItem& item, const TopLoc_Location& placement, double devicePixelRatio = 1.0);

  core::PmiId pmiId() const { return m_id; }
  bool isGraphical() const { return !m_presentation.IsNull(); }

  bool AcceptDisplayMode(const int mode) const override { return mode == 0; }

protected:
  void Compute(const OccHandle<PrsMgr_PresentationManager>& manager, const OccHandle<Prs3d_Presentation>& prs,
               const int mode) override;
  void ComputeSelection(const OccHandle<SelectMgr_Selection>& selection, const int mode) override;

private:
  void collectGeometry();

  core::PmiId m_id;
  TopoDS_Shape m_presentation; // prototype coordinates
  std::string m_label;         // semantic text when there is no graphical presentation
  gp_Pnt m_labelAnchor;
  std::optional<gp_Pnt> m_leaderTarget;
  double m_dpr;

  // Geometry in prototype coordinates (the object's local transformation places it).
  std::vector<std::vector<gp_Pnt>> m_polylines;
  std::vector<gp_Pnt> m_triangles; // 3 points per triangle
};

// All PMI of a model in the 3D view: one presentation per item and instance of
// the part it is defined in, visibility per item, cross-highlighting of the
// target geometry, and saved views (camera, visible PMI, clipping planes).
// Methods never redraw; the caller invalidates the view.
class PmiScene {
public:
  explicit PmiScene(const OccHandle<AIS_InteractiveContext>& context);
  ~PmiScene();

  PmiScene(const PmiScene&) = delete;
  PmiScene& operator=(const PmiScene&) = delete;

  // Shows all PMI by default.
  void setModel(std::shared_ptr<const core::PmiModel> pmi, std::shared_ptr<const core::AssemblyIndex> index,
                double devicePixelRatio = 1.0);
  void clear();
  const std::shared_ptr<const core::PmiModel>& model() const { return m_pmi; }

  void setVisible(const std::vector<core::PmiId>& ids, bool visible);
  void setOnlyVisible(const std::vector<core::PmiId>& ids);
  bool isVisible(core::PmiId id) const;
  std::vector<core::PmiId> visibleIds() const;

  // Selects the PMI in the 3D view and highlights the geometry they apply to.
  void highlight(const std::vector<core::PmiId>& ids);
  const std::vector<core::PmiId>& highlighted() const { return m_highlighted; }

  // PMI shown by an interactive object (from 3D picking), or kNoPmi.
  core::PmiId pmiOf(const OccHandle<AIS_InteractiveObject>& object) const;
  // PMI currently selected in the context.
  std::vector<core::PmiId> selectedPmi() const;

  // Applies a saved view: camera, which PMI are shown, clipping planes.
  void applyView(const core::PmiSavedView& view, const OccHandle<V3d_View>& v3dView);
  // Removes a saved view's clipping planes.
  void clearViewClipping(const OccHandle<V3d_View>& v3dView);

  int presentationCount() const;

private:
  struct Entry {
    std::vector<OccHandle<PmiPresentation>> placements;
    bool visible = true;
  };
  std::vector<TopLoc_Location> placementsOf(const core::PmiItem& item) const;
  void removeTargetHighlight();

  OccHandle<AIS_InteractiveContext> m_context;
  std::shared_ptr<const core::PmiModel> m_pmi;
  std::shared_ptr<const core::AssemblyIndex> m_index;
  std::vector<Entry> m_entries; // by PmiId
  std::unordered_map<const AIS_InteractiveObject*, core::PmiId> m_objectToPmi;
  std::vector<core::PmiId> m_highlighted;
  OccHandle<AIS_InteractiveObject> m_targetHighlight;
  std::vector<OccHandle<Graphic3d_ClipPlane>> m_clipPlanes;
};

} // namespace sc::render
