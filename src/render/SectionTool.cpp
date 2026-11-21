#include "render/SectionTool.h"

#include "render/SceneManager.h"

#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_Presentation.hxx>
#include <Select3D_SensitiveTriangle.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <SelectMgr_Selection.hxx>

#include <algorithm>
#include <cmath>

namespace sc::render {

namespace {

const Quantity_Color HandleColor(0.20, 0.55, 0.95, Quantity_TOC_sRGB);

void perpendicularAxes(const gp_Dir& normal, gp_Dir& u, gp_Dir& v) {
  const gp_Dir helper = std::abs(normal.Z()) < 0.9 ? gp_Dir(0, 0, 1) : gp_Dir(1, 0, 0);
  u = gp_Dir(gp_Vec(helper).Crossed(gp_Vec(normal)));
  v = gp_Dir(gp_Vec(normal).Crossed(gp_Vec(u)));
}

} // namespace

// Translucent rectangle in a section plane with an arrow along its normal: shows
// where the plane is and serves as the drag handle.
class SectionHandle : public AIS_InteractiveObject {
  DEFINE_STANDARD_RTTI_INLINE(SectionHandle, AIS_InteractiveObject)

public:
  SectionHandle() {
    SetInfiniteState(false);
    SetMutable(true);
    // OCCT's default hover/selection styles repaint the whole quad opaque, hiding the model.
    // Keep the handle translucent and just tint it brighter.
    for (const bool dynamic : {true, false}) {
      OccHandle<Prs3d_Drawer> style = new Prs3d_Drawer();
      style->SetColor(Quantity_Color(0.35, 0.75, 1.0, Quantity_TOC_sRGB));
      style->SetTransparency(0.6f);
      style->SetDisplayMode(0);
      style->SetZLayer(Graphic3d_ZLayerId_UNKNOWN); // same layer as the handle
      if (dynamic) {
        SetDynamicHilightAttributes(style);
      } else {
        SetHilightAttributes(style);
      }
    }
  }

  void setGeometry(const gp_Pnt& center, const gp_Dir& normal, double halfSize) {
    _center = center;
    _normal = normal;
    _halfSize = halfSize;
  }

  bool AcceptDisplayMode(const int mode) const override { return mode == 0; }

protected:
  void corners(std::array<gp_Pnt, 4>& out) const {
    gp_Dir u, v;
    perpendicularAxes(_normal, u, v);
    const gp_Vec du = gp_Vec(u) * _halfSize;
    const gp_Vec dv = gp_Vec(v) * _halfSize;
    out = {_center.Translated(-du - dv), _center.Translated(du - dv), _center.Translated(du + dv),
           _center.Translated(-du + dv)};
  }

  void Compute(const OccHandle<PrsMgr_PresentationManager>&, const OccHandle<Prs3d_Presentation>& prs,
               const int mode) override {
    if (mode != 0 || _halfSize <= 0.0) {
      return;
    }
    std::array<gp_Pnt, 4> cornerPoints;
    corners(cornerPoints);

    OccHandle<Graphic3d_Group> fillGroup = prs->NewGroup();
    OccHandle<Graphic3d_AspectFillArea3d> fill = new Graphic3d_AspectFillArea3d();
    fill->SetInteriorStyle(Aspect_IS_SOLID);
    fill->SetInteriorColor(Quantity_ColorRGBA(HandleColor, 0.15f));
    fill->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
    fill->SetFaceCulling(Graphic3d_TypeOfBackfacingModel_DoubleSided);
    fill->SetAlphaMode(Graphic3d_AlphaMode_Blend);
    fillGroup->SetGroupPrimitivesAspect(fill);
    OccHandle<Graphic3d_ArrayOfTriangles> quad = new Graphic3d_ArrayOfTriangles(6);
    for (const int i : {0, 1, 2, 0, 2, 3}) {
      quad->AddVertex(cornerPoints[static_cast<size_t>(i)]);
    }
    fillGroup->AddPrimitiveArray(quad);

    OccHandle<Graphic3d_Group> lineGroup = prs->NewGroup();
    lineGroup->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(HandleColor, Aspect_TOL_SOLID, 2.0));
    OccHandle<Graphic3d_ArrayOfPolylines> lines = new Graphic3d_ArrayOfPolylines(5 + 2 + 3, 3);
    lines->AddBound(5);
    for (const int i : {0, 1, 2, 3, 0}) {
      lines->AddVertex(cornerPoints[static_cast<size_t>(i)]);
    }
    // Arrow along the normal: the side that is cut away.
    const gp_Vec arrow = gp_Vec(_normal) * (_halfSize * 0.35);
    const gp_Pnt tip = _center.Translated(arrow);
    lines->AddBound(2);
    lines->AddVertex(_center);
    lines->AddVertex(tip);
    gp_Dir u, v;
    perpendicularAxes(_normal, u, v);
    lines->AddBound(3);
    lines->AddVertex(tip.Translated(-arrow * 0.25 + gp_Vec(u) * (_halfSize * 0.06)));
    lines->AddVertex(tip);
    lines->AddVertex(tip.Translated(-arrow * 0.25 - gp_Vec(u) * (_halfSize * 0.06)));
    lineGroup->AddPrimitiveArray(lines);
  }

  void ComputeSelection(const OccHandle<SelectMgr_Selection>& selection, const int mode) override {
    if (mode != 0 || _halfSize <= 0.0) {
      return;
    }
    std::array<gp_Pnt, 4> cornerPoints;
    corners(cornerPoints);
    OccHandle<SelectMgr_EntityOwner> owner = new SelectMgr_EntityOwner(this, 4);
    selection->Add(new Select3D_SensitiveTriangle(owner, cornerPoints[0], cornerPoints[1], cornerPoints[2]));
    selection->Add(new Select3D_SensitiveTriangle(owner, cornerPoints[0], cornerPoints[2], cornerPoints[3]));
  }

private:
  gp_Pnt _center;
  gp_Dir _normal{0, 0, 1};
  double _halfSize = 0.0;
};

gp_Pln SectionPlane::plane() const {
  return gp_Pln(gp_Pnt(0, 0, 0).Translated(gp_Vec(normal) * offset), normal);
}

SectionTool::SectionTool(OcctViewport* viewport, SceneManager* scene, QObject* parent)
    : QObject(parent), _viewport(viewport), _scene(scene), _sequence(new Graphic3d_SequenceOfHClipPlane()) {
  for (int i = 0; i < MaxPlanes; ++i) {
    OccHandle<Graphic3d_ClipPlane> clip = new Graphic3d_ClipPlane();
    clip->SetOn(true);
    // Caps take each part's own color so sections read like the parts they cut.
    clip->SetUseObjectMaterial(true);
    clip->SetCappingHatch(Aspect_HS_DIAGONAL_45);
    _clip[static_cast<size_t>(i)] = clip;
    _handles[static_cast<size_t>(i)] = new SectionHandle();
  }
  // Default orientations: X, Y, Z.
  _planes[0].normal = gp_Dir(1, 0, 0);
  _planes[1].normal = gp_Dir(0, 1, 0);
  _planes[2].normal = gp_Dir(0, 0, 1);
  viewport->setDragHandler(this);
}

SectionTool::~SectionTool() {
  _viewport->setDragHandler(nullptr);
  const OccHandle<AIS_InteractiveContext>& context = _viewport->context();
  if (!context.IsNull()) {
    for (const auto& handle : _handles) {
      context->Remove(handle, false);
    }
  }
  if (_scene != nullptr) {
    _scene->setClipPlanes({});
  }
}

void SectionTool::setBounds(const Bnd_Box& bounds) {
  _bounds = bounds;
  for (int i = 0; i < MaxPlanes; ++i) {
    // New model: center the planes in it.
    const auto [low, high] = offsetRange(i);
    _planes[static_cast<size_t>(i)].offset = (low + high) / 2.0;
    apply(i);
  }
}

std::pair<double, double> SectionTool::offsetRange(int index) const {
  if (_bounds.IsVoid()) {
    return {-100.0, 100.0};
  }
  const gp_Vec normal(plane(index).normal);
  double xmin, ymin, zmin, xmax, ymax, zmax;
  _bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
  double low = 1e300;
  double high = -1e300;
  for (const double x : {xmin, xmax}) {
    for (const double y : {ymin, ymax}) {
      for (const double z : {zmin, zmax}) {
        const double distance = gp_Vec(x, y, z).Dot(normal);
        low = std::min(low, distance);
        high = std::max(high, distance);
      }
    }
  }
  return {low, high};
}

void SectionTool::setPlane(int index, const SectionPlane& plane) {
  if (index < 0 || index >= MaxPlanes) {
    return;
  }
  _planes[static_cast<size_t>(index)] = plane;
  apply(index);
}

void SectionTool::flip(int index) {
  SectionPlane& plane = _planes.at(static_cast<size_t>(index));
  plane.normal.Reverse();
  plane.offset = -plane.offset;
  apply(index);
}

bool SectionTool::isActive() const {
  return std::any_of(_planes.begin(), _planes.end(), [](const SectionPlane& p) { return p.enabled; });
}

void SectionTool::reset() {
  for (int i = 0; i < MaxPlanes; ++i) {
    _planes[static_cast<size_t>(i)].enabled = false;
    apply(i);
  }
}

void SectionTool::setHandlesVisible(bool visible) {
  _handlesVisible = visible;
  for (int i = 0; i < MaxPlanes; ++i) {
    updateHandle(i);
  }
}

void SectionTool::apply(int index) {
  const SectionPlane& plane = _planes[static_cast<size_t>(index)];
  const OccHandle<Graphic3d_ClipPlane>& clip = _clip[static_cast<size_t>(index)];
  // OCCT keeps the half-space where the plane equation is positive; we cut away
  // the side the normal points to, so the equation uses the reversed normal.
  clip->SetEquation(gp_Pln(plane.plane().Location(), plane.normal.Reversed()));
  clip->SetCapping(plane.capping);
  if (plane.hatch) {
    clip->SetCappingHatchOn();
  } else {
    clip->SetCappingHatchOff();
  }
  updateClipSequence();
  updateHandle(index);
  Q_EMIT planeChanged(index);
}

void SectionTool::updateClipSequence() {
  // A fresh sequence each time: objects compare sequences by handle.
  _sequence = new Graphic3d_SequenceOfHClipPlane();
  for (int i = 0; i < MaxPlanes; ++i) {
    if (_planes[static_cast<size_t>(i)].enabled) {
      _sequence->Append(_clip[static_cast<size_t>(i)]);
    }
  }
  if (_scene != nullptr) {
    _scene->setClipPlanes(_sequence->IsEmpty() ? OccHandle<Graphic3d_SequenceOfHClipPlane>() : _sequence);
  }
}

void SectionTool::updateHandle(int index) {
  const OccHandle<AIS_InteractiveContext>& context = _viewport->context();
  if (context.IsNull()) {
    return;
  }
  const SectionPlane& plane = _planes[static_cast<size_t>(index)];
  const OccHandle<SectionHandle>& handle = _handles[static_cast<size_t>(index)];
  if (!plane.enabled || !_handlesVisible || _bounds.IsVoid()) {
    context->Remove(handle, false);
    return;
  }
  // Centered on the model's center projected onto the plane, a bit larger than the model.
  double xmin, ymin, zmin, xmax, ymax, zmax;
  _bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
  const gp_Pnt center((xmin + xmax) / 2.0, (ymin + ymax) / 2.0, (zmin + zmax) / 2.0);
  const gp_Vec normal(plane.normal);
  const double distance = gp_Vec(center.XYZ()).Dot(normal) - plane.offset;
  const gp_Pnt onPlane = center.Translated(-normal * distance);
  const double halfSize = 0.6 * std::sqrt(_bounds.SquareExtent()) / 2.0 * 1.2;
  handle->setGeometry(onPlane, plane.normal, halfSize);
  if (context->IsDisplayed(handle)) {
    context->Redisplay(handle, false);
    context->RecomputeSelectionOnly(handle);
  } else {
    context->Display(handle, 0, 0, false);
  }
}

double SectionTool::offsetPerPixel(int index, NCollection_Vec2<double>& screenDirection) const {
  // Project a unit step along the normal to the screen.
  const SectionPlane& plane = _planes[static_cast<size_t>(index)];
  const OccHandle<V3d_View>& view = _viewport->view();
  const double length = std::max(std::sqrt(_bounds.IsVoid() ? 1.0 : _bounds.SquareExtent()) / 4.0, 1e-6);
  const gp_Pnt a = plane.plane().Location();
  const gp_Pnt b = a.Translated(gp_Vec(plane.normal) * length);
  int ax = 0, ay = 0, bx = 0, by = 0;
  view->Convert(a.X(), a.Y(), a.Z(), ax, ay);
  view->Convert(b.X(), b.Y(), b.Z(), bx, by);
  const NCollection_Vec2<double> screenDelta(double(bx - ax), double(by - ay));
  const double pixels = screenDelta.Modulus();
  if (pixels < 8.0) {
    // Looking along the normal: drag vertically (up = towards the viewer side).
    screenDirection = NCollection_Vec2<double>(0.0, -1.0);
    return view->Convert(1);
  }
  screenDirection = screenDelta / pixels;
  return length / pixels;
}

bool SectionTool::beginDrag(const OccHandle<AIS_InteractiveObject>& detected, const NCollection_Vec2<int>&) {
  for (int i = 0; i < MaxPlanes; ++i) {
    if (_planes[static_cast<size_t>(i)].enabled && detected == _handles[static_cast<size_t>(i)]) {
      _dragIndex = i;
      _dragStartOffset = _planes[static_cast<size_t>(i)].offset;
      return true;
    }
  }
  return false;
}

void SectionTool::drag(const NCollection_Vec2<int>& from, const NCollection_Vec2<int>& to) {
  if (_dragIndex < 0) {
    return;
  }
  NCollection_Vec2<double> direction;
  const double perPixel = offsetPerPixel(_dragIndex, direction);
  const NCollection_Vec2<double> delta(double(to.x() - from.x()), double(to.y() - from.y()));
  const double moved = (delta.x() * direction.x() + delta.y() * direction.y()) * perPixel;
  const auto [low, high] = offsetRange(_dragIndex);
  SectionPlane plane = _planes[static_cast<size_t>(_dragIndex)];
  plane.offset = std::clamp(_dragStartOffset + moved, low, high);
  setPlane(_dragIndex, plane);
}

void SectionTool::endDrag() {
  _dragIndex = -1;
}

} // namespace sc::render
