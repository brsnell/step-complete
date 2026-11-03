#include "render/SectionTool.h"

#include "render/SceneManager.h"

#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_Presentation.hxx>
#include <Select3D_SensitiveTriangle.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <SelectMgr_Selection.hxx>

#include <algorithm>
#include <cmath>

namespace sc::render {

namespace {

const Quantity_Color kHandleColor(0.20, 0.55, 0.95, Quantity_TOC_sRGB);

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
  }

  void setGeometry(const gp_Pnt& center, const gp_Dir& normal, double halfSize) {
    m_center = center;
    m_normal = normal;
    m_halfSize = halfSize;
  }

  bool AcceptDisplayMode(const int mode) const override { return mode == 0; }

protected:
  void corners(std::array<gp_Pnt, 4>& out) const {
    gp_Dir u, v;
    perpendicularAxes(m_normal, u, v);
    const gp_Vec du = gp_Vec(u) * m_halfSize;
    const gp_Vec dv = gp_Vec(v) * m_halfSize;
    out = {m_center.Translated(-du - dv), m_center.Translated(du - dv), m_center.Translated(du + dv),
           m_center.Translated(-du + dv)};
  }

  void Compute(const OccHandle<PrsMgr_PresentationManager>&, const OccHandle<Prs3d_Presentation>& prs,
               const int mode) override {
    if (mode != 0 || m_halfSize <= 0.0) {
      return;
    }
    std::array<gp_Pnt, 4> c;
    corners(c);

    OccHandle<Graphic3d_Group> fillGroup = prs->NewGroup();
    OccHandle<Graphic3d_AspectFillArea3d> fill = new Graphic3d_AspectFillArea3d();
    fill->SetInteriorStyle(Aspect_IS_SOLID);
    fill->SetInteriorColor(Quantity_ColorRGBA(kHandleColor, 0.15f));
    fill->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
    fill->SetFaceCulling(Graphic3d_TypeOfBackfacingModel_DoubleSided);
    fill->SetAlphaMode(Graphic3d_AlphaMode_Blend);
    fillGroup->SetGroupPrimitivesAspect(fill);
    OccHandle<Graphic3d_ArrayOfTriangles> quad = new Graphic3d_ArrayOfTriangles(6);
    for (const int i : {0, 1, 2, 0, 2, 3}) {
      quad->AddVertex(c[static_cast<size_t>(i)]);
    }
    fillGroup->AddPrimitiveArray(quad);

    OccHandle<Graphic3d_Group> lineGroup = prs->NewGroup();
    lineGroup->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(kHandleColor, Aspect_TOL_SOLID, 2.0));
    OccHandle<Graphic3d_ArrayOfPolylines> lines = new Graphic3d_ArrayOfPolylines(5 + 2 + 3, 3);
    lines->AddBound(5);
    for (const int i : {0, 1, 2, 3, 0}) {
      lines->AddVertex(c[static_cast<size_t>(i)]);
    }
    // Arrow along the normal: the side that is cut away.
    const gp_Vec n = gp_Vec(m_normal) * (m_halfSize * 0.35);
    const gp_Pnt tip = m_center.Translated(n);
    lines->AddBound(2);
    lines->AddVertex(m_center);
    lines->AddVertex(tip);
    gp_Dir u, v;
    perpendicularAxes(m_normal, u, v);
    lines->AddBound(3);
    lines->AddVertex(tip.Translated(-n * 0.25 + gp_Vec(u) * (m_halfSize * 0.06)));
    lines->AddVertex(tip);
    lines->AddVertex(tip.Translated(-n * 0.25 - gp_Vec(u) * (m_halfSize * 0.06)));
    lineGroup->AddPrimitiveArray(lines);
  }

  void ComputeSelection(const OccHandle<SelectMgr_Selection>& selection, const int mode) override {
    if (mode != 0 || m_halfSize <= 0.0) {
      return;
    }
    std::array<gp_Pnt, 4> c;
    corners(c);
    OccHandle<SelectMgr_EntityOwner> owner = new SelectMgr_EntityOwner(this, 4);
    selection->Add(new Select3D_SensitiveTriangle(owner, c[0], c[1], c[2]));
    selection->Add(new Select3D_SensitiveTriangle(owner, c[0], c[2], c[3]));
  }

private:
  gp_Pnt m_center;
  gp_Dir m_normal{0, 0, 1};
  double m_halfSize = 0.0;
};

gp_Pln SectionPlane::plane() const {
  return gp_Pln(gp_Pnt(0, 0, 0).Translated(gp_Vec(normal) * offset), normal);
}

SectionTool::SectionTool(OcctViewport* viewport, SceneManager* scene, QObject* parent)
    : QObject(parent), m_viewport(viewport), m_scene(scene),
      m_sequence(new Graphic3d_SequenceOfHClipPlane()) {
  for (int i = 0; i < kMaxPlanes; ++i) {
    OccHandle<Graphic3d_ClipPlane> clip = new Graphic3d_ClipPlane();
    clip->SetOn(true);
    // Caps take each part's own color so sections read like the parts they cut.
    clip->SetUseObjectMaterial(true);
    clip->SetCappingHatch(Aspect_HS_DIAGONAL_45);
    m_clip[static_cast<size_t>(i)] = clip;
    m_handles[static_cast<size_t>(i)] = new SectionHandle();
  }
  // Default orientations: X, Y, Z.
  m_planes[0].normal = gp_Dir(1, 0, 0);
  m_planes[1].normal = gp_Dir(0, 1, 0);
  m_planes[2].normal = gp_Dir(0, 0, 1);
  viewport->setDragHandler(this);
}

SectionTool::~SectionTool() {
  m_viewport->setDragHandler(nullptr);
  const OccHandle<AIS_InteractiveContext>& context = m_viewport->context();
  if (!context.IsNull()) {
    for (const auto& handle : m_handles) {
      context->Remove(handle, false);
    }
  }
  if (m_scene != nullptr) {
    m_scene->setClipPlanes({});
  }
}

void SectionTool::setBounds(const Bnd_Box& bounds) {
  m_bounds = bounds;
  for (int i = 0; i < kMaxPlanes; ++i) {
    // New model: center the planes in it.
    const auto [low, high] = offsetRange(i);
    m_planes[static_cast<size_t>(i)].offset = (low + high) / 2.0;
    apply(i);
  }
}

std::pair<double, double> SectionTool::offsetRange(int index) const {
  if (m_bounds.IsVoid()) {
    return {-100.0, 100.0};
  }
  const gp_Vec n(plane(index).normal);
  double xmin, ymin, zmin, xmax, ymax, zmax;
  m_bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
  double low = 1e300;
  double high = -1e300;
  for (const double x : {xmin, xmax}) {
    for (const double y : {ymin, ymax}) {
      for (const double z : {zmin, zmax}) {
        const double d = gp_Vec(x, y, z).Dot(n);
        low = std::min(low, d);
        high = std::max(high, d);
      }
    }
  }
  return {low, high};
}

void SectionTool::setPlane(int index, const SectionPlane& plane) {
  if (index < 0 || index >= kMaxPlanes) {
    return;
  }
  m_planes[static_cast<size_t>(index)] = plane;
  apply(index);
}

void SectionTool::flip(int index) {
  SectionPlane& p = m_planes.at(static_cast<size_t>(index));
  p.normal.Reverse();
  p.offset = -p.offset;
  apply(index);
}

bool SectionTool::isActive() const {
  return std::any_of(m_planes.begin(), m_planes.end(), [](const SectionPlane& p) { return p.enabled; });
}

void SectionTool::reset() {
  for (int i = 0; i < kMaxPlanes; ++i) {
    m_planes[static_cast<size_t>(i)].enabled = false;
    apply(i);
  }
}

void SectionTool::setHandlesVisible(bool visible) {
  m_handlesVisible = visible;
  for (int i = 0; i < kMaxPlanes; ++i) {
    updateHandle(i);
  }
}

void SectionTool::apply(int index) {
  const SectionPlane& p = m_planes[static_cast<size_t>(index)];
  const OccHandle<Graphic3d_ClipPlane>& clip = m_clip[static_cast<size_t>(index)];
  // OCCT keeps the half-space where the plane equation is positive; we cut away
  // the side the normal points to, so the equation uses the reversed normal.
  clip->SetEquation(gp_Pln(p.plane().Location(), p.normal.Reversed()));
  clip->SetCapping(p.capping);
  if (p.hatch) {
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
  m_sequence = new Graphic3d_SequenceOfHClipPlane();
  for (int i = 0; i < kMaxPlanes; ++i) {
    if (m_planes[static_cast<size_t>(i)].enabled) {
      m_sequence->Append(m_clip[static_cast<size_t>(i)]);
    }
  }
  if (m_scene != nullptr) {
    m_scene->setClipPlanes(m_sequence->IsEmpty() ? OccHandle<Graphic3d_SequenceOfHClipPlane>() : m_sequence);
  }
}

void SectionTool::updateHandle(int index) {
  const OccHandle<AIS_InteractiveContext>& context = m_viewport->context();
  if (context.IsNull()) {
    return;
  }
  const SectionPlane& p = m_planes[static_cast<size_t>(index)];
  const OccHandle<SectionHandle>& handle = m_handles[static_cast<size_t>(index)];
  if (!p.enabled || !m_handlesVisible || m_bounds.IsVoid()) {
    context->Remove(handle, false);
    return;
  }
  // Centered on the model's center projected onto the plane, a bit larger than the model.
  double xmin, ymin, zmin, xmax, ymax, zmax;
  m_bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
  const gp_Pnt center((xmin + xmax) / 2.0, (ymin + ymax) / 2.0, (zmin + zmax) / 2.0);
  const gp_Vec n(p.normal);
  const double distance = gp_Vec(center.XYZ()).Dot(n) - p.offset;
  const gp_Pnt onPlane = center.Translated(-n * distance);
  const double halfSize = 0.6 * std::sqrt(m_bounds.SquareExtent()) / 2.0 * 1.2;
  handle->setGeometry(onPlane, p.normal, halfSize);
  if (context->IsDisplayed(handle)) {
    context->Redisplay(handle, false);
    context->RecomputeSelectionOnly(handle);
  } else {
    context->Display(handle, 0, 0, false);
  }
}

double SectionTool::offsetPerPixel(int index, NCollection_Vec2<double>& screenDirection) const {
  // Project a unit step along the normal to the screen.
  const SectionPlane& p = m_planes[static_cast<size_t>(index)];
  const OccHandle<V3d_View>& view = m_viewport->view();
  const double length = std::max(std::sqrt(m_bounds.IsVoid() ? 1.0 : m_bounds.SquareExtent()) / 4.0, 1e-6);
  const gp_Pnt a = p.plane().Location();
  const gp_Pnt b = a.Translated(gp_Vec(p.normal) * length);
  int ax = 0, ay = 0, bx = 0, by = 0;
  view->Convert(a.X(), a.Y(), a.Z(), ax, ay);
  view->Convert(b.X(), b.Y(), b.Z(), bx, by);
  const NCollection_Vec2<double> d(double(bx - ax), double(by - ay));
  const double pixels = d.Modulus();
  if (pixels < 8.0) {
    // Looking along the normal: drag vertically (up = towards the viewer side).
    screenDirection = NCollection_Vec2<double>(0.0, -1.0);
    return view->Convert(1);
  }
  screenDirection = d / pixels;
  return length / pixels;
}

bool SectionTool::beginDrag(const OccHandle<AIS_InteractiveObject>& detected, const NCollection_Vec2<int>&) {
  for (int i = 0; i < kMaxPlanes; ++i) {
    if (m_planes[static_cast<size_t>(i)].enabled && detected == m_handles[static_cast<size_t>(i)]) {
      m_dragIndex = i;
      m_dragStartOffset = m_planes[static_cast<size_t>(i)].offset;
      return true;
    }
  }
  return false;
}

void SectionTool::drag(const NCollection_Vec2<int>& from, const NCollection_Vec2<int>& to) {
  if (m_dragIndex < 0) {
    return;
  }
  NCollection_Vec2<double> direction;
  const double perPixel = offsetPerPixel(m_dragIndex, direction);
  const NCollection_Vec2<double> delta(double(to.x() - from.x()), double(to.y() - from.y()));
  const double moved = (delta.x() * direction.x() + delta.y() * direction.y()) * perPixel;
  const auto [low, high] = offsetRange(m_dragIndex);
  SectionPlane p = m_planes[static_cast<size_t>(m_dragIndex)];
  p.offset = std::clamp(m_dragStartOffset + moved, low, high);
  setPlane(m_dragIndex, p);
}

void SectionTool::endDrag() {
  m_dragIndex = -1;
}

} // namespace sc::render
