#pragma once

#include "core/Measurement.h"
#include "core/OccHandle.h"

#include <SelectMgr_EntityOwner.hxx>

#include <QObject>

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
// computed on the exact B-rep in world coordinates, its witness geometry is
// drawn, and measured() is emitted; the next click starts a new measurement.
// Clicking a picked entity again un-picks it.
class MeasureTool : public QObject {
  Q_OBJECT

public:
  MeasureTool(OcctViewport* viewport, SceneManager* scene, QObject* parent = nullptr);
  ~MeasureTool() override;

  // nullopt turns measuring off and restores whole-part selection.
  void setMode(std::optional<core::MeasureMode> mode);
  std::optional<core::MeasureMode> mode() const { return m_mode; }
  bool isActive() const { return m_mode.has_value(); }

  void setDisplayOptions(const core::MeasureDisplayOptions& options) { m_options = options; }
  const core::MeasureDisplayOptions& displayOptions() const { return m_options; }

  int pickCount() const { return static_cast<int>(m_picks.size()); }

  // Shows a result's witness (e.g. when it is chosen in the results list) or
  // removes it. The caller redraws the viewport.
  void showWitness(const core::Witness& witness);
  void clearWitness();
  bool hasWitness() const;

  // Drops pending picks and clears the selection, e.g. when the model changes.
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
  };

  std::optional<core::Pick> makePick(const OccHandle<SelectMgr_EntityOwner>& owner) const;
  std::vector<int> selectionModesFor(core::MeasureMode mode) const;
  void finish();

  OcctViewport* m_viewport;
  SceneManager* m_scene;
  std::optional<core::MeasureMode> m_mode;
  core::MeasureDisplayOptions m_options;
  std::vector<PendingPick> m_picks;
  OccHandle<WitnessPresentation> m_witness;
  bool m_updatingSelection = false;
};

} // namespace sc::render
