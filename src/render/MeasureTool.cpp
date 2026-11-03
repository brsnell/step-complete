#include "render/MeasureTool.h"

#include "render/OcctViewport.h"
#include "render/SceneManager.h"
#include "render/WitnessPresentation.h"

#include <StdSelect_ViewerSelector3d.hxx>

#include <algorithm>

namespace sc::render {

namespace {

// Point-to-point snapping radius around vertices and edge midpoints.
constexpr double kSnapPixels = 10.0;

} // namespace

MeasureTool::MeasureTool(OcctViewport* viewport, SceneManager* scene, QObject* parent)
    : QObject(parent), m_viewport(viewport), m_scene(scene) {
  connect(viewport, &OcctViewport::selectionChanged, this, &MeasureTool::onSelectionChanged);
  connect(viewport, &OcctViewport::hoverUpdated, this, &MeasureTool::onHoverUpdated);
}

MeasureTool::~MeasureTool() {
  clearWitness();
}

std::vector<int> MeasureTool::selectionModesFor(core::MeasureMode mode) const {
  switch (mode) {
  case core::MeasureMode::Angle:
  case core::MeasureMode::Radius:
    return SceneManager::subShapeModes(false, true, true);
  case core::MeasureMode::EntityInfo:
  case core::MeasureMode::Distance:
  case core::MeasureMode::PointToPoint:
  case core::MeasureMode::MassProperties: // not a pick mode; never set on the tool
    break;
  }
  return SceneManager::subShapeModes(true, true, true);
}

void MeasureTool::setMode(std::optional<core::MeasureMode> mode) {
  if (mode == m_mode) {
    return;
  }
  m_picks.clear();
  m_mode = mode;
  m_updatingSelection = true;
  m_scene->setSelectionModes(mode ? selectionModesFor(*mode) : std::vector<int>{});
  m_updatingSelection = false;
  m_viewport->setAccumulatingSelection(mode.has_value());
  if (!mode) {
    clearWitness();
  }
  m_viewport->setOverlayMarker(std::nullopt);
  m_viewport->invalidateScene();
  Q_EMIT pickProgress(0, mode ? core::picksRequired(*mode) : 0);
}

void MeasureTool::reset() {
  m_picks.clear();
  clearWitness();
  if (!m_viewport->context().IsNull()) {
    m_updatingSelection = true;
    m_viewport->context()->ClearSelected(false);
    m_updatingSelection = false;
  }
  if (m_mode) {
    Q_EMIT pickProgress(0, core::picksRequired(*m_mode));
  }
}

std::optional<core::Pick> MeasureTool::makePick(const OccHandle<SelectMgr_EntityOwner>& owner) const {
  const auto entity = m_scene->pickedEntity(owner);
  if (!entity) {
    return std::nullopt;
  }
  core::Pick pick;
  pick.shape = entity->worldShape;
  pick.owner = m_scene->model()->displayPath(entity->part);

  // The 3D point under the cursor when this owner was clicked; otherwise a
  // representative point of the entity.
  const OccHandle<AIS_InteractiveContext>& context = m_viewport->context();
  if (context->HasDetected() && context->DetectedOwner() == owner) {
    pick.point = context->MainSelector()->PickedPoint(1);
  } else {
    const core::measure::EntityInfo info = core::measure::entityInfo(pick.shape);
    pick.point = info.edge ? info.edge->midpoint : info.face ? info.face->centroid : info.point;
  }
  return pick;
}

void MeasureTool::onSelectionChanged() {
  if (!m_mode || m_updatingSelection) {
    return;
  }
  const OccHandle<AIS_InteractiveContext>& context = m_viewport->context();

  std::vector<OccHandle<SelectMgr_EntityOwner>> selected;
  for (context->InitSelected(); context->MoreSelected(); context->NextSelected()) {
    selected.push_back(context->SelectedOwner());
  }

  // Un-picked (clicked again): drop.
  m_picks.erase(std::remove_if(m_picks.begin(), m_picks.end(),
                               [&selected](const PendingPick& p) {
                                 return std::find(selected.begin(), selected.end(), p.owner) ==
                                        selected.end();
                               }),
                m_picks.end());

  // Newly picked: add in selection order; anything that is not a part entity is deselected.
  std::vector<OccHandle<SelectMgr_EntityOwner>> rejected;
  for (const auto& owner : selected) {
    const bool known = std::any_of(m_picks.begin(), m_picks.end(),
                                   [&owner](const PendingPick& p) { return p.owner == owner; });
    if (known) {
      continue;
    }
    if (std::optional<core::Pick> pick = makePick(owner)) {
      m_picks.push_back({owner, std::move(*pick)});
    } else {
      rejected.push_back(owner);
    }
  }
  if (!rejected.empty()) {
    m_updatingSelection = true;
    for (const auto& owner : rejected) {
      context->AddOrRemoveSelected(owner, false);
    }
    m_updatingSelection = false;
  }

  const int required = core::picksRequired(*m_mode);
  Q_EMIT pickProgress(static_cast<int>(m_picks.size()), required);
  if (static_cast<int>(m_picks.size()) >= required) {
    finish();
  }
}

void MeasureTool::onHoverUpdated() {
  if (m_mode != core::MeasureMode::PointToPoint) {
    return;
  }
  const OccHandle<AIS_InteractiveContext>& context = m_viewport->context();
  const auto entity = context->HasDetected() ? m_scene->pickedEntity(context->DetectedOwner()) : std::nullopt;
  if (!entity) {
    m_viewport->setOverlayMarker(std::nullopt);
    return;
  }
  // No exact face projection here: this runs on every mouse move.
  const core::SnapResult snapped = core::snap(entity->worldShape, context->MainSelector()->PickedPoint(1),
                                              m_viewport->pixelsToModel(kSnapPixels), false);
  m_viewport->setOverlayMarker(
      OcctViewport::OverlayMarker{snapped.point, QString::fromUtf8(core::toString(snapped.kind))});
}

void MeasureTool::finish() {
  std::vector<core::Pick> picks;
  for (const PendingPick& p : m_picks) {
    picks.push_back(p.pick);
  }
  m_picks.clear();
  const core::Measurement result =
      core::computeMeasurement(*m_mode, picks, m_options, m_viewport->pixelsToModel(kSnapPixels));

  m_updatingSelection = true;
  m_viewport->context()->ClearSelected(false);
  m_updatingSelection = false;
  if (result.ok) {
    showWitness(result.witness);
  } else {
    clearWitness();
  }
  m_viewport->invalidateScene();
  Q_EMIT pickProgress(0, core::picksRequired(*m_mode));
  Q_EMIT measured(result);
}

void MeasureTool::showWitness(const core::Witness& witness) {
  clearWitness();
  if (witness.empty() || m_viewport->context().IsNull()) {
    return;
  }
  m_witness = new WitnessPresentation(witness, m_viewport->devicePixelRatioF());
  // Display without selection modes: witnesses never take picks.
  m_viewport->context()->Display(m_witness, 0, -1, false);
}

void MeasureTool::clearWitness() {
  if (!m_witness.IsNull() && !m_viewport->context().IsNull()) {
    m_viewport->context()->Remove(m_witness, false);
  }
  m_witness.Nullify();
}

bool MeasureTool::hasWitness() const {
  return !m_witness.IsNull();
}

} // namespace sc::render
