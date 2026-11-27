#pragma once

#include "core/AssemblyIndex.h"
#include "core/Measurement.h"
#include "core/OccHandle.h"
#include "core/SectionCap.h"

#include <AIS_Shape.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <TopoDS_Face.hxx>

#include <QObject>

#include <map>
#include <optional>
#include <vector>

namespace sc::render {

class OcctViewport;
class SceneManager;
class WitnessPresentation;

// Interactive measurement in the 3D view.
//
// While a mode is active, parts are pickable by vertex/edge/face (whichever
// the mode accepts), hovering pre-highlights the entity under the cursor and
// each click adds a pick. When the mode has enough picks the result is
// computed on the exact B-rep in world coordinates and measured() is emitted;
// the next click starts a new measurement. Clicking a picked entity again
// un-picks it.
//
// With a section view on (docs/designs/section-aware-measurement.md), a click on a capped cut
// face picks that face (the part's cross-section) rather than the hidden wall behind it.
//
// The tool also draws the witness geometry of the results the caller wants
// on screen (setWitnesses): any number at once, one of them emphasized.
class MeasureTool : public QObject {
  Q_OBJECT

public:
  MeasureTool(OcctViewport* viewport, SceneManager* scene, QObject* parent = nullptr);
  ~MeasureTool() override;

  // nullopt turns measuring off and restores whole-part selection.
  void setMode(std::optional<core::MeasureMode> mode);
  std::optional<core::MeasureMode> mode() const { return _mode; }
  bool isActive() const { return _mode.has_value(); }

  void setDisplayOptions(const core::MeasureDisplayOptions& options) { _options = options; }
  const core::MeasureDisplayOptions& displayOptions() const { return _options; }

  int pickCount() const { return static_cast<int>(_picks.size()); }
  // Pending picks that are section faces (drawn in the selection color on top).
  int sectionFacePickCount() const;

  struct ShownWitness {
    int id = 0; // stable per result: the same id keeps its presentation
    core::Witness witness;
    bool emphasized = false;
  };
  // Draws exactly these witnesses: new ids are added, missing ids removed and
  // changed emphasis restyled. Empty witnesses are skipped. The caller redraws
  // the viewport.
  void setWitnesses(const std::vector<ShownWitness>& witnesses);
  void clearWitnesses();
  int witnessCount() const { return static_cast<int>(_witnesses.size()); }
  bool hasWitness(int id) const { return _witnesses.count(id) != 0; }
  bool isEmphasized(int id) const;

  // Drops pending picks, witnesses and the selection, e.g. when the model changes.
  void reset();

public Q_SLOTS:
  // Connected to OcctViewport::selectionChanged; public for tests.
  void onSelectionChanged();
  // Connected to OcctViewport::hoverUpdated: in Point to Point mode, shows
  // where a click would snap. Public for tests.
  void onHoverUpdated();

Q_SIGNALS:
  void measured(const sc::core::Measurement& measurement);
  // After each pick: how many are collected and how many the mode needs.
  void pickProgress(int picked, int required);

private:
  struct PendingPick {
    OccHandle<SelectMgr_EntityOwner> owner;
    core::Pick pick;
    OccHandle<AIS_Shape> sectionHighlight; // the cut face, for a section face pick
  };
  struct SectionFace {
    TopoDS_Face face;
    gp_Pnt point;
  };

  std::optional<core::Pick> makePick(const OccHandle<SelectMgr_EntityOwner>& owner);
  // The capped cut face of 'part' a click that hit 'hitPoint' went through, if any.
  std::optional<SectionFace> sectionFaceAt(core::NodeId part, const gp_Pnt& hitPoint);
  void showSectionHighlight(PendingPick& pending);
  void clearPicks();
  std::vector<int> selectionModesFor(core::MeasureMode mode) const;
  void finish();

  OcctViewport* _viewport;
  SceneManager* _scene;
  std::optional<core::MeasureMode> _mode;
  core::MeasureDisplayOptions _options;
  std::vector<PendingPick> _picks;
  std::map<int, OccHandle<WitnessPresentation>> _witnesses;
  // Cap faces by part and plane, for the planes they were computed with.
  const core::AssemblyIndex* _capModel = nullptr;
  std::vector<core::CutPlane> _capPlanes;
  std::map<std::pair<core::NodeId, size_t>, TopoDS_Shape> _capFaces;
  bool _updatingSelection = false;
};

} // namespace sc::render
