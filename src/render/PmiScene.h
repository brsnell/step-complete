#pragma once

#include "core/AssemblyIndex.h"
#include "core/OccHandle.h"
#include "core/PmiModel.h"
#include "render/PmiLabel.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <Graphic3d_ClipPlane.hxx>
#include <V3d_View.hxx>
#include <gp_Vec.hxx>

#include <memory>
#include <unordered_map>
#include <vector>

namespace sc::render {

// An arrowhead of a semantic-only dimension: a filled triangle whose tip is at a
// world point, of constant size in device pixels (Graphic3d_TMF_ZoomPers: it keeps its
// orientation in the model plane but does not scale with the zoom). Not selectable.
class PmiArrow : public AIS_InteractiveObject {
  DEFINE_STANDARD_RTTI_INLINE(PmiArrow, AIS_InteractiveObject)

public:
  // `dir` points from the tail to the tip; `side` is in the arrow's plane, perpendicular to `dir`.
  PmiArrow(const gp_Pnt& tip, const gp_Dir& dir, const gp_Dir& side, double devicePixelRatio);

  static double length(double devicePixelRatio);
  static double halfWidth(double devicePixelRatio);

  bool AcceptDisplayMode(const int mode) const override { return mode == 0; }

protected:
  void Compute(const OccHandle<PrsMgr_PresentationManager>& manager, const OccHandle<Prs3d_Presentation>& prs,
               const int mode) override;
  void ComputeSelection(const OccHandle<SelectMgr_Selection>&, const int) override {}

private:
  gp_Dir _dir;
  gp_Dir _side;
  double _dpr;
};

// Draws one PMI item at one placement: the file's graphical presentation
// (polylines and triangulated text/symbols) when present. Items without one get
// semantic geometry: for linear dimensions extension lines and a dimension line
// through the text position, for diameter/radius/angular dimensions an arrowed
// leader, otherwise a plain leader line to the geometry the item applies to (the
// semantic text itself is a separate screen-space PmiLabel, see PmiScene).
// Arrowheads are separate PmiArrow objects (arrows()). Selectable as a whole (mode 0).
class PmiPresentation : public AIS_InteractiveObject {
  DEFINE_STANDARD_RTTI_INLINE(PmiPresentation, AIS_InteractiveObject)

public:
  PmiPresentation(const core::PmiItem& item, const TopLoc_Location& placement, double devicePixelRatio = 1.0);

  core::PmiId pmiId() const { return _id; }
  bool isGraphical() const { return !_presentation.IsNull(); }
  // Semantic-only items have a text label at labelAnchor() (prototype coordinates).
  bool hasLabel() const { return !isGraphical() && !_label.empty(); }
  const gp_Pnt& labelAnchor() const { return _labelAnchor; }

  // Arrowheads of the semantic dimension geometry, in prototype coordinates.
  struct ArrowSpec {
    gp_Pnt tip;
    gp_Dir dir;  // tail to tip
    gp_Dir side; // in the annotation plane, perpendicular to dir
  };
  const std::vector<ArrowSpec>& arrows() const { return _arrows; }

  bool AcceptDisplayMode(const int mode) const override { return mode == 0; }

protected:
  void Compute(const OccHandle<PrsMgr_PresentationManager>& manager, const OccHandle<Prs3d_Presentation>& prs,
               const int mode) override;
  void ComputeSelection(const OccHandle<SelectMgr_Selection>& selection, const int mode) override;

private:
  void collectGeometry();
  void buildDimensionGeometry(const core::PmiItem& item);
  bool buildLinear(const core::PmiItem& item);
  void buildArrowedLeader(const core::PmiItem& item, bool angular, bool diameter);

  core::PmiId _id;
  TopoDS_Shape _presentation; // prototype coordinates
  std::string _label;         // semantic text when there is no graphical presentation
  gp_Pnt _labelAnchor;
  std::optional<gp_Pnt> _leaderTarget;
  double _dpr;

  // Geometry in prototype coordinates (the object's local transformation places it).
  std::vector<std::vector<gp_Pnt>> _polylines;
  std::vector<gp_Pnt> _triangles; // 3 points per triangle
  // Semantic dimension geometry (prototype coordinates).
  std::vector<std::pair<gp_Pnt, gp_Pnt>> _segments; // extension and dimension lines
  std::vector<ArrowSpec> _arrows;
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
  const std::shared_ptr<const core::PmiModel>& model() const { return _pmi; }
  // Rebuilds the presentations at a new device-pixel ratio (line widths, arrowheads and labels
  // are sized in device pixels), keeping visibility, highlight and the semantic-labels switch.
  // Labels go back to their base positions: call declutter() afterwards. No-op for the same ratio.
  void setDevicePixelRatio(double devicePixelRatio);
  double devicePixelRatio() const { return _dpr; }

  void setVisible(const std::vector<core::PmiId>& ids, bool visible);
  void setOnlyVisible(const std::vector<core::PmiId>& ids);
  bool isVisible(core::PmiId id) const;
  std::vector<core::PmiId> visibleIds() const;

  // Selects the PMI in the 3D view and highlights the geometry they apply to.
  void highlight(const std::vector<core::PmiId>& ids);
  const std::vector<core::PmiId>& highlighted() const { return _highlighted; }

  // PMI shown by an interactive object (from 3D picking), or NoPmi.
  core::PmiId pmiOf(const OccHandle<AIS_InteractiveObject>& object) const;
  // PMI currently selected in the context.
  std::vector<core::PmiId> selectedPmi() const;

  // Applies a saved view: camera, which PMI are shown, clipping planes.
  void applyView(const core::PmiSavedView& view, const OccHandle<V3d_View>& v3dView);
  // Removes a saved view's clipping planes.
  void clearViewClipping(const OccHandle<V3d_View>& v3dView);

  // Exploded view (docs/designs/exploded-view.md): a world translation per assembly node id,
  // applied to the PMI of each part instance; PMI defined at the top level stays. Empty puts
  // everything back. Labels move with their part: call declutter() afterwards.
  void setPartOffsets(std::vector<gp_Vec> offsets);

  int presentationCount() const;
  // Moves overlapping semantic labels apart for the camera of `view` as it is now (labels are
  // screen-space boxes, so overlap depends on the camera). Never called implicitly: call it
  // once the view has settled (after fit or a saved view, window resize). Repeated calls start
  // from the base positions. It is O(labels) and must not be called per frame.
  void declutter(const OccHandle<V3d_View>& view);

  // Semantic-only presentation (text labels, leaders, dimension lines and arrows of items
  // without file graphics). setModel() picks the default: on when fewer than half of the items
  // have graphics from the file, off otherwise (the file's graphics then tell the story and
  // the labels would duplicate them). Selection and target highlighting work either way.
  void setSemanticLabelsVisible(bool visible);
  bool semanticLabelsVisible() const { return _semanticLabels; }

  // Arrowheads drawn for semantic-only dimensions.
  int arrowCount() const;
  // The screen-space labels of a semantic-only item (one per placement).
  const std::vector<OccHandle<PmiLabel>>& labelsOf(core::PmiId id) const;

private:
  struct Entry {
    std::vector<OccHandle<PmiPresentation>> placements;
    std::vector<OccHandle<PmiLabel>> labels; // semantic-only items: one per placement
    std::vector<OccHandle<PmiArrow>> arrows; // semantic-only dimensions: per placement and end
    // The part instance of each placement, label and arrow (NoNode: the top level).
    std::vector<core::NodeId> placementNodes;
    std::vector<core::NodeId> labelNodes;
    std::vector<core::NodeId> arrowNodes;
    bool visible = true;
  };
  struct Placement {
    core::NodeId node = core::NoNode; // the part instance, NoNode at the top level
    TopLoc_Location location;
  };
  std::vector<Placement> placementsOf(const core::PmiItem& item) const;
  gp_Vec offsetOf(core::NodeId node) const;
  void applyOffsets();
  void build(const std::vector<bool>& visibility);
  void removePresentations();
  void removeTargetHighlight();
  void updateDisplay(Entry& entry);

  OccHandle<AIS_InteractiveContext> _context;
  std::shared_ptr<const core::PmiModel> _pmi;
  std::shared_ptr<const core::AssemblyIndex> _index;
  std::vector<Entry> _entries; // by PmiId
  std::unordered_map<const AIS_InteractiveObject*, core::PmiId> _objectToPmi;
  std::vector<core::PmiId> _highlighted;
  OccHandle<AIS_InteractiveObject> _targetHighlight;
  std::vector<OccHandle<Graphic3d_ClipPlane>> _clipPlanes;
  double _dpr = 1.0;
  bool _semanticLabels = true;
  std::vector<gp_Vec> _offsets; // exploded view, by NodeId
};

} // namespace sc::render
