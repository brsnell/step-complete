#include "render/MeasureTool.h"

#include "render/OcctViewport.h"
#include "render/SceneManager.h"
#include "render/WitnessPresentation.h"

#include <StdSelect_ViewerSelector3d.hxx>

#include <algorithm>

namespace sc::render {

namespace {

// Point-to-point snapping radius around vertices and edge midpoints.
constexpr double SnapPixels = 10.0;

} // namespace

MeasureTool::MeasureTool(OcctViewport* viewport, SceneManager* scene, QObject* parent)
    : QObject(parent), _viewport(viewport), _scene(scene) {
  connect(viewport, &OcctViewport::selectionChanged, this, &MeasureTool::onSelectionChanged);
  connect(viewport, &OcctViewport::hoverUpdated, this, &MeasureTool::onHoverUpdated);
}

MeasureTool::~MeasureTool() {
  clearWitnesses();
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
  if (mode == _mode) {
    return;
  }
  _picks.clear();
  _mode = mode;
  _updatingSelection = true;
  _scene->setSelectionModes(mode ? selectionModesFor(*mode) : std::vector<int>{});
  _updatingSelection = false;
  _viewport->setAccumulatingSelection(mode.has_value());
  _viewport->setOverlayMarker(std::nullopt);
  _viewport->invalidateScene();
  Q_EMIT pickProgress(0, mode ? core::picksRequired(*mode) : 0);
}

void MeasureTool::reset() {
  _picks.clear();
  clearWitnesses();
  if (!_viewport->context().IsNull()) {
    _updatingSelection = true;
    _viewport->context()->ClearSelected(false);
    _updatingSelection = false;
  }
  if (_mode) {
    Q_EMIT pickProgress(0, core::picksRequired(*_mode));
  }
}

std::optional<core::Pick> MeasureTool::makePick(const OccHandle<SelectMgr_EntityOwner>& owner) const {
  const auto entity = _scene->pickedEntity(owner);
  if (!entity) {
    return std::nullopt;
  }
  core::Pick pick;
  pick.shape = entity->worldShape;
  pick.owner = _scene->model()->displayPath(entity->part);

  // The 3D point under the cursor when this owner was clicked; otherwise a
  // representative point of the entity.
  const OccHandle<AIS_InteractiveContext>& context = _viewport->context();
  if (context->HasDetected() && context->DetectedOwner() == owner) {
    pick.point = context->MainSelector()->PickedPoint(1);
  } else {
    const core::measure::EntityInfo info = core::measure::entityInfo(pick.shape);
    pick.point = info.edge ? info.edge->midpoint : info.face ? info.face->centroid : info.point;
  }
  return pick;
}

void MeasureTool::onSelectionChanged() {
  if (!_mode || _updatingSelection) {
    return;
  }
  const OccHandle<AIS_InteractiveContext>& context = _viewport->context();

  std::vector<OccHandle<SelectMgr_EntityOwner>> selected;
  for (context->InitSelected(); context->MoreSelected(); context->NextSelected()) {
    selected.push_back(context->SelectedOwner());
  }

  // Un-picked (clicked again): drop.
  _picks.erase(std::remove_if(_picks.begin(), _picks.end(),
                              [&selected](const PendingPick& pick) {
                                return std::find(selected.begin(), selected.end(), pick.owner) ==
                                       selected.end();
                              }),
               _picks.end());

  // Newly picked: add in selection order; anything that is not a part entity is deselected.
  std::vector<OccHandle<SelectMgr_EntityOwner>> rejected;
  for (const auto& owner : selected) {
    const bool known = std::any_of(_picks.begin(), _picks.end(),
                                   [&owner](const PendingPick& p) { return p.owner == owner; });
    if (known) {
      continue;
    }
    if (std::optional<core::Pick> pick = makePick(owner)) {
      _picks.push_back({owner, std::move(*pick)});
    } else {
      rejected.push_back(owner);
    }
  }
  if (!rejected.empty()) {
    _updatingSelection = true;
    for (const auto& owner : rejected) {
      context->AddOrRemoveSelected(owner, false);
    }
    _updatingSelection = false;
  }

  const int required = core::picksRequired(*_mode);
  Q_EMIT pickProgress(static_cast<int>(_picks.size()), required);
  if (static_cast<int>(_picks.size()) >= required) {
    finish();
  }
}

void MeasureTool::onHoverUpdated() {
  if (_mode != core::MeasureMode::PointToPoint) {
    return;
  }
  const OccHandle<AIS_InteractiveContext>& context = _viewport->context();
  const auto entity = context->HasDetected() ? _scene->pickedEntity(context->DetectedOwner()) : std::nullopt;
  if (!entity) {
    _viewport->setOverlayMarker(std::nullopt);
    return;
  }
  // No exact face projection here: this runs on every mouse move.
  const core::SnapResult snapped = core::snap(entity->worldShape, context->MainSelector()->PickedPoint(1),
                                              _viewport->pixelsToModel(SnapPixels), false);
  _viewport->setOverlayMarker(
      OcctViewport::OverlayMarker{snapped.point, QString::fromUtf8(core::toString(snapped.kind))});
}

void MeasureTool::finish() {
  std::vector<core::Pick> picks;
  for (const PendingPick& pick : _picks) {
    picks.push_back(pick.pick);
  }
  _picks.clear();
  const core::Measurement result =
      core::computeMeasurement(*_mode, picks, _options, _viewport->pixelsToModel(SnapPixels));

  _updatingSelection = true;
  _viewport->context()->ClearSelected(false);
  _updatingSelection = false;
  _viewport->invalidateScene();
  Q_EMIT pickProgress(0, core::picksRequired(*_mode));
  Q_EMIT measured(result);
}

void MeasureTool::setWitnesses(const std::vector<ShownWitness>& witnesses) {
  const OccHandle<AIS_InteractiveContext>& context = _viewport->context();
  if (context.IsNull()) {
    _witnesses.clear();
    return;
  }
  const auto wanted = [&witnesses](int id) {
    return std::any_of(witnesses.begin(), witnesses.end(),
                       [id](const ShownWitness& w) { return w.id == id && !w.witness.empty(); });
  };
  for (auto it = _witnesses.begin(); it != _witnesses.end();) {
    if (wanted(it->first)) {
      ++it;
    } else {
      context->Remove(it->second, false);
      it = _witnesses.erase(it);
    }
  }
  for (const ShownWitness& shown : witnesses) {
    if (shown.witness.empty()) {
      continue;
    }
    auto it = _witnesses.find(shown.id);
    const double dpr = _viewport->devicePixelRatioF();
    if (it != _witnesses.end() && it->second->devicePixelRatio() != dpr) {
      // The window moved to a screen with other scaling: rebuild at the new line widths.
      context->Remove(it->second, false);
      _witnesses.erase(it);
      it = _witnesses.end();
    }
    if (it == _witnesses.end()) {
      OccHandle<WitnessPresentation> prs = new WitnessPresentation(shown.witness, dpr);
      prs->setEmphasized(shown.emphasized);
      // Display without selection modes: witnesses never take picks.
      context->Display(prs, 0, -1, false);
      _witnesses.emplace(shown.id, prs);
    } else if (it->second->isEmphasized() != shown.emphasized) {
      it->second->setEmphasized(shown.emphasized);
      context->Redisplay(it->second, false);
    }
  }
}

void MeasureTool::clearWitnesses() {
  const OccHandle<AIS_InteractiveContext>& context = _viewport->context();
  if (!context.IsNull()) {
    for (const auto& [id, prs] : _witnesses) {
      context->Remove(prs, false);
    }
  }
  _witnesses.clear();
}

bool MeasureTool::isEmphasized(int id) const {
  const auto it = _witnesses.find(id);
  return it != _witnesses.end() && it->second->isEmphasized();
}

} // namespace sc::render
