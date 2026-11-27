#include "render/MeasureTool.h"

#include "render/OcctViewport.h"
#include "render/SceneManager.h"
#include "render/WitnessPresentation.h"

#include <Graphic3d_Camera.hxx>
#include <Graphic3d_ClipPlane.hxx>
#include <Graphic3d_SequenceOfHClipPlane.hxx>
#include <Prs3d_Drawer.hxx>
#include <StdSelect_ViewerSelector3d.hxx>

#include <algorithm>
#include <cmath>

namespace sc::render {

namespace {

// Point-to-point snapping radius around vertices and edge midpoints.
constexpr double SnapPixels = 10.0;

// The section planes that are on, with normals towards the side they cut away (OCCT keeps the
// side its plane's normal points to).
std::vector<core::CutPlane> cutPlanes(const OccHandle<Graphic3d_SequenceOfHClipPlane>& sequence) {
  std::vector<core::CutPlane> planes;
  if (sequence.IsNull()) {
    return planes;
  }
  for (Graphic3d_SequenceOfHClipPlane::Iterator it(*sequence); it.More(); it.Next()) {
    const OccHandle<Graphic3d_ClipPlane>& clip = it.Value();
    if (clip.IsNull() || !clip->IsOn()) {
      continue;
    }
    const gp_Pln& kept = clip->ToPlane();
    planes.push_back(
        core::CutPlane{gp_Pln(kept.Location(), kept.Axis().Direction().Reversed()), clip->IsCapping()});
  }
  return planes;
}

bool samePlanes(const std::vector<core::CutPlane>& a, const std::vector<core::CutPlane>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    double a1, b1, c1, d1, a2, b2, c2, d2;
    a[i].plane.Coefficients(a1, b1, c1, d1);
    b[i].plane.Coefficients(a2, b2, c2, d2);
    if (a[i].capped != b[i].capped || std::abs(a1 - a2) > 1e-12 || std::abs(b1 - b2) > 1e-12 ||
        std::abs(c1 - c2) > 1e-12 || std::abs(d1 - d2) > 1e-9) {
      return false;
    }
  }
  return true;
}

} // namespace

MeasureTool::MeasureTool(OcctViewport* viewport, SceneManager* scene, QObject* parent)
    : QObject(parent), _viewport(viewport), _scene(scene) {
  connect(viewport, &OcctViewport::selectionChanged, this, &MeasureTool::onSelectionChanged);
  connect(viewport, &OcctViewport::hoverUpdated, this, &MeasureTool::onHoverUpdated);
}

MeasureTool::~MeasureTool() {
  clearPicks();
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
  clearPicks();
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
  clearPicks();
  clearWitnesses();
  _capModel = nullptr;
  _capPlanes.clear();
  _capFaces.clear();
  if (!_viewport->context().IsNull()) {
    _updatingSelection = true;
    _viewport->context()->ClearSelected(false);
    _updatingSelection = false;
  }
  if (_mode) {
    Q_EMIT pickProgress(0, core::picksRequired(*_mode));
  }
}

std::optional<core::Pick> MeasureTool::makePick(const OccHandle<SelectMgr_EntityOwner>& owner) {
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
    pick.point = SceneManager::pickedPoint(context, owner);
    if (std::optional<SectionFace> section = sectionFaceAt(entity->part, pick.point)) {
      pick.shape = section->face;
      pick.point = section->point;
      pick.sectionFace = true;
      if (_mode == core::MeasureMode::PointToPoint) {
        // Where the hover marker showed it would snap: a corner or edge of the cut face.
        pick.point =
            core::snapOnCap(section->face, section->point, _viewport->pixelsToModel(SnapPixels)).point;
      }
    }
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

  // A click on a section cap toggles the hidden wall behind it in OCCT's selection. The cap
  // pick is kept apart from that selection (its owner is null), so the wall stays pickable on
  // its own: undo the toggle and remember the cap.
  std::optional<core::Pick> capClick;
  std::optional<core::Pick> detectedPick; // a plain pick of the detected owner, made below anyway
  std::vector<OccHandle<SelectMgr_EntityOwner>> toggleBack;
  if (context->HasDetected()) {
    const OccHandle<SelectMgr_EntityOwner> owner = context->DetectedOwner();
    const auto isOwner = [&owner](const PendingPick& p) { return p.owner == owner; };
    const bool nowSelected = std::find(selected.begin(), selected.end(), owner) != selected.end();
    const bool wasPicked = std::any_of(_picks.begin(), _picks.end(), isOwner);
    if (nowSelected != wasPicked) {
      std::optional<core::Pick> pick = makePick(owner);
      if (pick && pick->sectionFace) {
        capClick = std::move(pick);
        toggleBack.push_back(owner);
        if (nowSelected) {
          selected.erase(std::find(selected.begin(), selected.end(), owner));
        } else {
          selected.push_back(owner);
        }
      } else {
        detectedPick = std::move(pick);
      }
    }
  }

  // Un-picked (clicked again): drop. Section face picks are not in the selection.
  for (auto it = _picks.begin(); it != _picks.end();) {
    if (!it->owner.IsNull() && std::find(selected.begin(), selected.end(), it->owner) == selected.end()) {
      it = _picks.erase(it);
    } else {
      ++it;
    }
  }

  // Newly picked: add in selection order; anything that is not a part entity is deselected.
  for (const auto& owner : selected) {
    const bool known = std::any_of(_picks.begin(), _picks.end(),
                                   [&owner](const PendingPick& p) { return p.owner == owner; });
    if (known) {
      continue;
    }
    std::optional<core::Pick> pick =
        detectedPick && owner == context->DetectedOwner() ? std::move(detectedPick) : makePick(owner);
    if (pick) {
      _picks.push_back({owner, std::move(*pick), {}});
    } else {
      toggleBack.push_back(owner);
    }
  }
  if (capClick) {
    // The same cut face again un-picks it; in Point to Point only at the same point, since its
    // corners are picked through one face.
    const bool byPoint = _mode == core::MeasureMode::PointToPoint;
    const double samePoint = _viewport->pixelsToModel(SnapPixels) / 2.0;
    const auto sameFace = [&capClick, byPoint, samePoint](const PendingPick& p) {
      return p.pick.sectionFace && p.pick.shape.IsSame(capClick->shape) &&
             (!byPoint || p.pick.point.Distance(capClick->point) <= samePoint);
    };
    const auto existing = std::find_if(_picks.begin(), _picks.end(), sameFace);
    if (existing != _picks.end()) {
      context->Remove(existing->sectionHighlight, false); // clicked again: un-pick
      _picks.erase(existing);
    } else {
      _picks.push_back({OccHandle<SelectMgr_EntityOwner>(), std::move(*capClick), {}});
      showSectionHighlight(_picks.back());
    }
  }
  if (!toggleBack.empty()) {
    _updatingSelection = true;
    for (const auto& owner : toggleBack) {
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
  // No exact face projection here: this runs on every mouse move. Over a section cap, snap on
  // the cut face (its faces are cached per part and plane).
  const gp_Pnt point = SceneManager::pickedPoint(context, context->DetectedOwner());
  const double snapDistance = _viewport->pixelsToModel(SnapPixels);
  const std::optional<SectionFace> section = sectionFaceAt(entity->part, point);
  const core::SnapResult snapped = section ? core::snapOnCap(section->face, section->point, snapDistance)
                                           : core::snap(entity->worldShape, point, snapDistance, false);
  _viewport->setOverlayMarker(
      OcctViewport::OverlayMarker{snapped.point, QString::fromUtf8(core::toString(snapped.kind))});
}

void MeasureTool::finish() {
  std::vector<core::Pick> picks;
  for (const PendingPick& pick : _picks) {
    picks.push_back(pick.pick);
  }
  clearPicks();
  const core::Measurement result =
      core::computeMeasurement(*_mode, picks, _options, _viewport->pixelsToModel(SnapPixels));

  _updatingSelection = true;
  _viewport->context()->ClearSelected(false);
  _updatingSelection = false;
  _viewport->invalidateScene();
  Q_EMIT pickProgress(0, core::picksRequired(*_mode));
  Q_EMIT measured(result);
}

std::optional<MeasureTool::SectionFace> MeasureTool::sectionFaceAt(core::NodeId part,
                                                                   const gp_Pnt& hitPoint) {
  const std::vector<core::CutPlane> planes = cutPlanes(_scene->clipPlanes());
  const auto& model = _scene->model();
  // Wireframe draws no caps: what the user sees through the cut is what was hit.
  if (planes.empty() || !model || part == core::NoNode || _scene->displayStyle() == DisplayStyle::Wireframe) {
    return std::nullopt;
  }
  if (model.get() != _capModel || !samePlanes(planes, _capPlanes)) {
    _capModel = model.get();
    _capPlanes = planes;
    _capFaces.clear();
  }
  // The ray from the eye through the hit.
  const OccHandle<Graphic3d_Camera>& camera = _viewport->view()->Camera();
  gp_Dir direction = camera->Direction();
  if (!camera->IsOrthographic()) {
    const gp_Vec toHit(camera->Eye(), hitPoint);
    if (toHit.Magnitude() > gp::Resolution()) {
      direction = gp_Dir(toHit);
    }
  }
  const std::optional<core::CapHit> hit = core::capCrossing(hitPoint, direction, planes);
  if (!hit ||
      (!camera->IsOrthographic() && hit->point.Distance(hitPoint) >= camera->Eye().Distance(hitPoint))) {
    return std::nullopt; // no cap in front, or (perspective) the crossing is behind the eye
  }
  // With the cap faces cached, they answer whether the crossing is on this part (Point to Point
  // hovers run this on every mouse move); otherwise classify against the solids before the boolean.
  const auto key = std::make_pair(part, hit->plane);
  auto cached = _capFaces.find(key);
  if (cached == _capFaces.end()) {
    const TopoDS_Shape shape = model->worldShape(part);
    if (!core::capHit(shape, hitPoint, direction, planes)) {
      return std::nullopt;
    }
    cached = _capFaces.emplace(key, core::capFaces(shape, planes, hit->plane)).first;
  }
  const std::optional<TopoDS_Face> face = core::faceAt(cached->second, hit->point);
  if (!face) {
    return std::nullopt;
  }
  return SectionFace{*face, hit->point};
}

void MeasureTool::showSectionHighlight(PendingPick& pending) {
  // The part's own highlight is on the far wall behind the cap: draw the cut face on top.
  const OccHandle<AIS_InteractiveContext>& context = _viewport->context();
  OccHandle<AIS_Shape> highlight = new AIS_Shape(pending.pick.shape);
  highlight->SetColor(context->SelectionStyle()->Color());
  highlight->SetTransparency(0.3);
  highlight->SetZLayer(Graphic3d_ZLayerId_Topmost);
  context->Display(highlight, AIS_Shaded, -1, false);
  pending.sectionHighlight = highlight;
}

void MeasureTool::clearPicks() {
  const OccHandle<AIS_InteractiveContext>& context = _viewport->context();
  for (const PendingPick& pending : _picks) {
    if (!pending.sectionHighlight.IsNull() && !context.IsNull()) {
      context->Remove(pending.sectionHighlight, false);
    }
  }
  _picks.clear();
}

void MeasureTool::dropSectionFacePicks() {
  const OccHandle<AIS_InteractiveContext>& context = _viewport->context();
  const auto before = _picks.size();
  for (auto it = _picks.begin(); it != _picks.end();) {
    if (it->pick.sectionFace) {
      if (!context.IsNull() && !it->sectionHighlight.IsNull()) {
        context->Remove(it->sectionHighlight, false);
      }
      it = _picks.erase(it);
    } else {
      ++it;
    }
  }
  _capFaces.clear();
  if (_picks.size() != before && _mode) {
    Q_EMIT pickProgress(static_cast<int>(_picks.size()), core::picksRequired(*_mode));
  }
}

int MeasureTool::sectionFacePickCount() const {
  return static_cast<int>(std::count_if(_picks.begin(), _picks.end(),
                                        [](const PendingPick& pending) { return pending.pick.sectionFace; }));
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
