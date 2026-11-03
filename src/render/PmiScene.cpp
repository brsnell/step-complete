#include "render/PmiScene.h"

#include <AIS_Shape.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Poly_Polygon3D.hxx>
#include <Poly_Triangulation.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Select3D_SensitiveSegment.hxx>
#include <Select3D_SensitiveTriangle.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <SelectMgr_Selection.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Circ.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Sphere.hxx>

#include <algorithm>
#include <cmath>

namespace sc::render {

namespace {

// Annotations: dark blue, readable on the light background and on part colors.
const Quantity_Color kPmiColor(0.05, 0.20, 0.55, Quantity_TOC_sRGB);
// Geometry a selected annotation applies to.
const Quantity_Color kTargetColor(1.0, 0.62, 0.10, Quantity_TOC_sRGB);
// Semantic labels whose anchors are closer than this (model units) are stacked.
constexpr double kSameAnchorDistance = 1e-3;
// Arrowheads in logical pixels (scaled by the device pixel ratio).
constexpr double kArrowLength = 11.0;
constexpr double kArrowHalfWidth = 3.5;
// Extension lines run this fraction of the dimension-line offset past the dimension line.
constexpr double kExtensionOvershoot = 0.1;

std::optional<gp_Pnt> centerOf(const std::vector<TopoDS_Shape>& shapes) {
  Bnd_Box box;
  for (const TopoDS_Shape& shape : shapes) {
    if (!shape.IsNull()) {
      BRepBndLib::Add(shape, box, false);
    }
  }
  if (box.IsVoid()) {
    return std::nullopt;
  }
  double xmin, ymin, zmin, xmax, ymax, zmax;
  box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
  return gp_Pnt((xmin + xmax) / 2.0, (ymin + ymax) / 2.0, (zmin + zmax) / 2.0);
}

// Points along an edge: its stored polyline (tessellated PMI has no curve) or a
// discretization of its curve.
std::vector<gp_Pnt> edgePoints(const TopoDS_Edge& edge) {
  std::vector<gp_Pnt> points;
  TopLoc_Location location;
  const OccHandle<Poly_Polygon3D> polygon = BRep_Tool::Polygon3D(edge, location);
  if (!polygon.IsNull()) {
    const gp_Trsf trsf = location.Transformation();
    for (int i = 1; i <= polygon->NbNodes(); ++i) {
      points.push_back(polygon->Nodes().Value(i).Transformed(trsf));
    }
    return points;
  }
  if (!BRep_Tool::IsGeometric(edge) || BRep_Tool::Degenerated(edge)) {
    return points;
  }
  const BRepAdaptor_Curve curve(edge);
  const GCPnts_TangentialDeflection sampler(curve, 0.2, 0.01);
  for (int i = 1; i <= sampler.NbPoints(); ++i) {
    points.push_back(sampler.Value(i));
  }
  return points;
}

// --- semantic dimension geometry ---------------------------------------------------------

enum class DimensionShape { None, Linear, Diameter, Radius, Angular };

bool containsText(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

DimensionShape classify(const core::PmiItem& item) {
  if (item.kind != core::PmiKind::Dimension) {
    return DimensionShape::None;
  }
  const std::string& type = item.typeName;
  if (containsText(type, "iameter")) {
    return DimensionShape::Diameter;
  }
  if (containsText(type, "adius")) {
    return DimensionShape::Radius;
  }
  if (containsText(type, "Angular")) {
    return DimensionShape::Angular;
  }
  if (containsText(type, "Linear distance") || containsText(type, "Oriented distance")) {
    return DimensionShape::Linear;
  }
  return DimensionShape::None;
}

// A unit vector perpendicular to `dir` (stable for any direction).
gp_Dir anyPerpendicular(const gp_Dir& dir) {
  const gp_Dir axis = std::abs(dir.Z()) < 0.9 ? gp::DZ() : gp::DX();
  return gp_Dir(dir.Crossed(axis));
}

// A circular feature: cylinder, sphere or circle edge.
struct Circular {
  gp_Pnt center; // for a cylinder: the point of its axis nearest to the hint
  std::optional<gp_Dir> axis;
  double radius = 0.0;
};

std::optional<Circular> circularFeature(const TopoDS_Shape& shape, const gp_Pnt& hint) {
  if (shape.IsNull()) {
    return std::nullopt;
  }
  try {
    TopExp_Explorer face(shape, TopAbs_FACE);
    if (shape.ShapeType() == TopAbs_FACE || face.More()) {
      const TopoDS_Face f =
          shape.ShapeType() == TopAbs_FACE ? TopoDS::Face(shape) : TopoDS::Face(face.Current());
      const BRepAdaptor_Surface surface(f);
      if (surface.GetType() == GeomAbs_Cylinder) {
        const gp_Cylinder cylinder = surface.Cylinder();
        const gp_Ax1 axis = cylinder.Axis();
        const gp_Vec along(axis.Direction());
        return Circular{axis.Location().Translated(along * gp_Vec(axis.Location(), hint).Dot(along)),
                        axis.Direction(), cylinder.Radius()};
      }
      if (surface.GetType() == GeomAbs_Sphere) {
        const gp_Sphere sphere = surface.Sphere();
        return Circular{sphere.Location(), std::nullopt, sphere.Radius()};
      }
      return std::nullopt;
    }
    TopExp_Explorer edge(shape, TopAbs_EDGE);
    if (shape.ShapeType() == TopAbs_EDGE || edge.More()) {
      const TopoDS_Edge e =
          shape.ShapeType() == TopAbs_EDGE ? TopoDS::Edge(shape) : TopoDS::Edge(edge.Current());
      const BRepAdaptor_Curve curve(e);
      if (curve.GetType() == GeomAbs_Circle) {
        const gp_Circ circle = curve.Circle();
        return Circular{circle.Location(), circle.Axis().Direction(), circle.Radius()};
      }
    }
  } catch (const Standard_Failure&) {
  }
  return std::nullopt;
}

// Direction in which a leader leaves a circular feature: toward `from` when that is off
// the axis, else along the annotation plane's X direction, else any radial direction.
gp_Dir radialDirection(const Circular& circle, const gp_Pnt& from, const std::optional<gp_Ax2>& plane) {
  const auto radial = [&circle](gp_Vec v) -> std::optional<gp_Dir> {
    if (circle.axis) {
      v -= gp_Vec(*circle.axis) * v.Dot(gp_Vec(*circle.axis));
    }
    return v.Magnitude() > 1e-9 ? std::optional<gp_Dir>(gp_Dir(v)) : std::nullopt;
  };
  if (auto dir = radial(gp_Vec(circle.center, from))) {
    return *dir;
  }
  if (plane) {
    if (auto dir = radial(gp_Vec(plane->XDirection()))) {
      return *dir;
    }
  }
  return anyPerpendicular(circle.axis ? *circle.axis : gp::DZ());
}

// Normal of the (first) planar face of `shape`.
std::optional<gp_Dir> planarNormal(const TopoDS_Shape& shape) {
  if (shape.IsNull()) {
    return std::nullopt;
  }
  try {
    TopExp_Explorer face(shape, TopAbs_FACE);
    if (shape.ShapeType() == TopAbs_FACE || face.More()) {
      const TopoDS_Face f =
          shape.ShapeType() == TopAbs_FACE ? TopoDS::Face(shape) : TopoDS::Face(face.Current());
      const BRepAdaptor_Surface surface(f);
      if (surface.GetType() == GeomAbs_Plane) {
        return surface.Plane().Axis().Direction();
      }
    }
  } catch (const Standard_Failure&) {
  }
  return std::nullopt;
}

} // namespace

// --- PmiPresentation -------------------------------------------------------------------

PmiPresentation::PmiPresentation(const core::PmiItem& item, const TopLoc_Location& placement, double dpr)
    : m_id(item.id), m_presentation(item.presentation), m_dpr(dpr > 0.0 ? dpr : 1.0) {
  SetLocalTransformation(placement.Transformation());

  // Semantic label for items without a graphical presentation.
  // A generated name alone carries no information: items neither interpreted nor graphical
  // are not drawn at all.
  if (item.semantic || !item.presentation.IsNull()) {
    m_label = !item.text.empty() ? item.text : item.name;
  }
  m_leaderTarget = item.point1 ? item.point1 : centerOf(item.targets);
  if (item.textPosition) {
    m_labelAnchor = *item.textPosition;
  } else if (m_leaderTarget) {
    m_labelAnchor = *m_leaderTarget;
  }
  if (m_leaderTarget && m_leaderTarget->Distance(m_labelAnchor) < 1e-9) {
    m_leaderTarget.reset();
  }
  collectGeometry();
  if (m_presentation.IsNull() && !m_label.empty()) {
    buildDimensionGeometry(item);
  }
}

// Linear: extension lines from the attachment points to a dimension line that passes
// through the text position, parallel to the measured direction, with an arrowhead at each
// end, all in the annotation plane. Diameter/radius: a leader with an arrow on the circular
// feature. Angular: one arrowed leader per attachment point. Anything the data cannot
// support keeps the plain leader.
void PmiPresentation::buildDimensionGeometry(const core::PmiItem& item) {
  switch (classify(item)) {
  case DimensionShape::Linear:
    if (buildLinear(item)) {
      m_leaderTarget.reset();
    }
    break;
  case DimensionShape::Diameter:
    buildArrowedLeader(item, false, true);
    break;
  case DimensionShape::Radius:
    buildArrowedLeader(item, false, false);
    break;
  case DimensionShape::Angular:
    buildArrowedLeader(item, true, false);
    break;
  case DimensionShape::None:
    break;
  }
}

bool PmiPresentation::buildLinear(const core::PmiItem& item) {
  std::optional<gp_Pnt> a = item.point1 ? item.point1 : centerOf(item.targets);
  std::optional<gp_Pnt> b = item.point2 ? item.point2 : centerOf(item.secondTargets);
  if (!a || !b) {
    return false;
  }
  std::optional<gp_Pnt> textAt = item.textPosition;
  std::optional<std::pair<gp_Pnt, gp_Pnt>> lateral; // extension line when b is moved onto a's normal
  if (!textAt) {
    // No placement in the file: measure between the features (along the normal of the first
    // one when it is a plane) and put the text halfway.
    const auto normal =
        !item.point2 && !item.targets.empty() ? planarNormal(item.targets.front()) : std::nullopt;
    if (normal) {
      const gp_Pnt onNormal = a->Translated(gp_Vec(*normal) * gp_Vec(*a, *b).Dot(gp_Vec(*normal)));
      if (onNormal.Distance(*b) > 1e-6) {
        lateral = std::make_pair(onNormal, *b);
      }
      b = onNormal;
    }
    textAt = gp_Pnt((a->X() + b->X()) / 2.0, (a->Y() + b->Y()) / 2.0, (a->Z() + b->Z()) / 2.0);
  }
  const gp_Vec span(*a, *b);
  const double length = span.Magnitude();
  if (length < 1e-9) {
    return false;
  }
  if (!item.textPosition) {
    m_labelAnchor = *textAt;
    if (lateral) {
      m_segments.push_back(*lateral);
    }
  }
  const gp_Dir dir(span);
  std::optional<gp_Dir> normal;
  if (item.plane && std::abs(item.plane->Direction().Dot(dir)) < 0.99) {
    normal = item.plane->Direction();
  }

  // Offset of the dimension line: the text position's distance from the measured line, in the plane.
  gp_Vec offset(*a, *textAt);
  offset -= gp_Vec(dir) * offset.Dot(gp_Vec(dir));
  if (normal) {
    offset -= gp_Vec(*normal) * offset.Dot(gp_Vec(*normal));
  }
  const double offsetLength = offset.Magnitude();
  const bool offsetLine = offsetLength > 1e-6 * length;
  const gp_Dir side = normal ? gp_Dir(gp_Vec(*normal).Crossed(gp_Vec(dir)))
                             : (offsetLine ? gp_Dir(offset) : anyPerpendicular(dir));

  gp_Pnt q1 = *a;
  gp_Pnt q2 = *b;
  if (offsetLine) {
    q1.Translate(offset);
    q2.Translate(offset);
    const gp_Vec overshoot = offset * kExtensionOvershoot;
    m_segments.emplace_back(*a, q1.Translated(overshoot));
    m_segments.emplace_back(*b, q2.Translated(overshoot));
  }
  // The dimension line also reaches a text position outside the extension lines.
  const double along = gp_Vec(q1, *textAt).Dot(gp_Vec(dir));
  gp_Pnt lineStart = q1;
  gp_Pnt lineEnd = q2;
  if (along < 0.0) {
    lineStart = q1.Translated(gp_Vec(dir) * along);
  } else if (along > length) {
    lineEnd = q1.Translated(gp_Vec(dir) * along);
  }
  m_segments.emplace_back(lineStart, lineEnd);
  m_arrows.push_back({q1, dir.Reversed(), side});
  m_arrows.push_back({q2, dir, side});
  return true;
}

void PmiPresentation::buildArrowedLeader(const core::PmiItem& item, bool angular, bool diameter) {
  const auto arrowSide = [&item](const gp_Dir& dir) {
    const bool usePlane = item.plane && std::abs(item.plane->Direction().Dot(dir)) < 0.99;
    return usePlane ? gp_Dir(gp_Vec(item.plane->Direction()).Crossed(gp_Vec(dir))) : anyPerpendicular(dir);
  };

  if (!item.textPosition) {
    // Without a text position only a circular feature can be drawn: a diameter across it or
    // a radius from its center, in the plane perpendicular to its axis.
    if (angular) {
      return;
    }
    std::optional<gp_Pnt> hint = item.point1 ? item.point1 : centerOf(item.targets);
    for (size_t i = 0; hint && i < item.targets.size(); ++i) {
      const std::optional<Circular> circle = circularFeature(item.targets[i], *hint);
      if (!circle || circle->radius < 1e-9) {
        continue;
      }
      const gp_Dir u = radialDirection(*circle, circle->center, item.plane);
      const gp_Dir side = circle->axis ? gp_Dir(gp_Vec(*circle->axis).Crossed(gp_Vec(u))) : arrowSide(u);
      const gp_Pnt tip = circle->center.Translated(gp_Vec(u) * circle->radius);
      m_labelAnchor = circle->center;
      m_leaderTarget.reset();
      if (diameter) {
        const gp_Pnt opposite = circle->center.Translated(gp_Vec(u) * -circle->radius);
        m_segments.emplace_back(opposite, tip);
        m_arrows.push_back({opposite, u.Reversed(), side});
      } else {
        m_segments.emplace_back(circle->center, tip);
      }
      m_arrows.push_back({tip, u, side});
      return;
    }
    return;
  }

  const gp_Pnt& from = *item.textPosition;
  std::vector<gp_Pnt> tips;
  if (angular) {
    if (item.point1 && item.point2) {
      tips = {*item.point1, *item.point2};
    }
  } else {
    std::optional<gp_Pnt> rim = item.point1;
    for (size_t i = 0; !rim && i < item.targets.size(); ++i) {
      if (const auto circle = circularFeature(item.targets[i], from)) {
        rim = circle->center.Translated(gp_Vec(radialDirection(*circle, from, item.plane)) * circle->radius);
      }
    }
    if (!rim) {
      rim = m_leaderTarget;
    }
    if (rim) {
      tips = {*rim};
    }
  }
  bool any = false;
  for (const gp_Pnt& tip : tips) {
    const gp_Vec v(from, tip);
    if (v.Magnitude() < 1e-9) {
      continue;
    }
    const gp_Dir dir(v);
    m_arrows.push_back({tip, dir, arrowSide(dir)});
    if (angular) {
      m_segments.emplace_back(from, tip);
    } else {
      m_leaderTarget = tip; // the leader now ends at the arrow tip
    }
    any = true;
  }
  if (angular && any) {
    m_leaderTarget.reset();
  }
}

void PmiPresentation::collectGeometry() {
  if (m_presentation.IsNull()) {
    return;
  }
  for (TopExp_Explorer it(m_presentation, TopAbs_EDGE); it.More(); it.Next()) {
    std::vector<gp_Pnt> points = edgePoints(TopoDS::Edge(it.Current()));
    if (points.size() >= 2) {
      m_polylines.push_back(std::move(points));
    }
  }
  for (TopExp_Explorer it(m_presentation, TopAbs_FACE); it.More(); it.Next()) {
    TopLoc_Location location;
    const OccHandle<Poly_Triangulation> mesh = BRep_Tool::Triangulation(TopoDS::Face(it.Current()), location);
    if (mesh.IsNull()) {
      continue;
    }
    const gp_Trsf trsf = location.Transformation();
    for (int i = 1; i <= mesh->NbTriangles(); ++i) {
      int n1 = 0, n2 = 0, n3 = 0;
      mesh->Triangle(i).Get(n1, n2, n3);
      m_triangles.push_back(mesh->Node(n1).Transformed(trsf));
      m_triangles.push_back(mesh->Node(n2).Transformed(trsf));
      m_triangles.push_back(mesh->Node(n3).Transformed(trsf));
    }
  }
}

void PmiPresentation::Compute(const OccHandle<PrsMgr_PresentationManager>&,
                              const OccHandle<Prs3d_Presentation>& prs, const int mode) {
  if (mode != 0) {
    return;
  }
  if (!m_polylines.empty()) {
    int vertices = 0;
    for (const auto& polyline : m_polylines) {
      vertices += static_cast<int>(polyline.size());
    }
    OccHandle<Graphic3d_Group> group = prs->NewGroup();
    group->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(kPmiColor, Aspect_TOL_SOLID, 1.0 * m_dpr));
    OccHandle<Graphic3d_ArrayOfPolylines> lines =
        new Graphic3d_ArrayOfPolylines(vertices, static_cast<int>(m_polylines.size()));
    for (const auto& polyline : m_polylines) {
      lines->AddBound(static_cast<int>(polyline.size()));
      for (const gp_Pnt& p : polyline) {
        lines->AddVertex(p);
      }
    }
    group->AddPrimitiveArray(lines);
  }
  if (!m_triangles.empty()) {
    OccHandle<Graphic3d_Group> group = prs->NewGroup();
    OccHandle<Graphic3d_AspectFillArea3d> fill = new Graphic3d_AspectFillArea3d();
    fill->SetInteriorStyle(Aspect_IS_SOLID);
    fill->SetInteriorColor(kPmiColor);
    fill->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
    fill->SetFaceCulling(Graphic3d_TypeOfBackfacingModel_DoubleSided);
    group->SetGroupPrimitivesAspect(fill);
    OccHandle<Graphic3d_ArrayOfTriangles> triangles =
        new Graphic3d_ArrayOfTriangles(static_cast<int>(m_triangles.size()));
    for (const gp_Pnt& p : m_triangles) {
      triangles->AddVertex(p);
    }
    group->AddPrimitiveArray(triangles);
  }
  if (!hasLabel()) {
    return;
  }

  // Semantic-only: dimension and extension lines (arrowheads are PmiArrow objects).
  if (!m_segments.empty()) {
    OccHandle<Graphic3d_Group> group = prs->NewGroup();
    group->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(kPmiColor, Aspect_TOL_SOLID, 1.0 * m_dpr));
    OccHandle<Graphic3d_ArrayOfSegments> lines =
        new Graphic3d_ArrayOfSegments(static_cast<int>(2 * m_segments.size()));
    for (const auto& segment : m_segments) {
      lines->AddVertex(segment.first);
      lines->AddVertex(segment.second);
    }
    group->AddPrimitiveArray(lines);
  }

  // The leader line; the text is a PmiLabel at the anchor.
  if (m_leaderTarget) {
    OccHandle<Graphic3d_Group> leader = prs->NewGroup();
    leader->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(kPmiColor, Aspect_TOL_SOLID, 1.0 * m_dpr));
    OccHandle<Graphic3d_ArrayOfSegments> segment = new Graphic3d_ArrayOfSegments(2);
    segment->AddVertex(m_labelAnchor);
    segment->AddVertex(*m_leaderTarget);
    leader->AddPrimitiveArray(segment);
  }
}

void PmiPresentation::ComputeSelection(const OccHandle<SelectMgr_Selection>& selection, const int mode) {
  if (mode != 0) {
    return;
  }
  OccHandle<SelectMgr_EntityOwner> owner = new SelectMgr_EntityOwner(this, 6);
  for (const auto& polyline : m_polylines) {
    for (size_t i = 1; i < polyline.size(); ++i) {
      selection->Add(new Select3D_SensitiveSegment(owner, polyline[i - 1], polyline[i]));
    }
  }
  for (size_t i = 0; i + 2 < m_triangles.size(); i += 3) {
    selection->Add(
        new Select3D_SensitiveTriangle(owner, m_triangles[i], m_triangles[i + 1], m_triangles[i + 2]));
  }
  if (hasLabel() && m_leaderTarget) {
    selection->Add(new Select3D_SensitiveSegment(owner, m_labelAnchor, *m_leaderTarget));
  }
  if (hasLabel()) {
    for (const auto& segment : m_segments) {
      selection->Add(new Select3D_SensitiveSegment(owner, segment.first, segment.second));
    }
  }
}

// --- PmiArrow --------------------------------------------------------------------------

double PmiArrow::length(double dpr) {
  return std::round(kArrowLength * dpr);
}

double PmiArrow::halfWidth(double dpr) {
  return std::round(kArrowHalfWidth * dpr);
}

PmiArrow::PmiArrow(const gp_Pnt& tip, const gp_Dir& dir, const gp_Dir& side, double dpr)
    : m_dir(dir), m_side(side), m_dpr(dpr > 0.0 ? dpr : 1.0) {
  // Local coordinates are device pixels around the tip; orientation stays that of the model.
  SetTransformPersistence(new Graphic3d_TransformPers(Graphic3d_TMF_ZoomPers, tip));
  SetZLayer(Graphic3d_ZLayerId_Topmost);
}

void PmiArrow::Compute(const OccHandle<PrsMgr_PresentationManager>&, const OccHandle<Prs3d_Presentation>& prs,
                       const int mode) {
  if (mode != 0) {
    return;
  }
  OccHandle<Graphic3d_Group> group = prs->NewGroup();
  OccHandle<Graphic3d_AspectFillArea3d> fill = new Graphic3d_AspectFillArea3d();
  fill->SetInteriorStyle(Aspect_IS_SOLID);
  fill->SetInteriorColor(kPmiColor);
  fill->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
  fill->SetFaceCulling(Graphic3d_TypeOfBackfacingModel_DoubleSided);
  group->SetGroupPrimitivesAspect(fill);
  const gp_Vec back = gp_Vec(m_dir) * -length(m_dpr);
  const gp_Vec wing = gp_Vec(m_side) * halfWidth(m_dpr);
  OccHandle<Graphic3d_ArrayOfTriangles> triangle = new Graphic3d_ArrayOfTriangles(3);
  triangle->AddVertex(gp_Pnt(0.0, 0.0, 0.0));
  triangle->AddVertex(gp_Pnt(0.0, 0.0, 0.0).Translated(back + wing));
  triangle->AddVertex(gp_Pnt(0.0, 0.0, 0.0).Translated(back - wing));
  group->AddPrimitiveArray(triangle);
}

// --- PmiScene --------------------------------------------------------------------------

PmiScene::PmiScene(const OccHandle<AIS_InteractiveContext>& context) : m_context(context) {}

PmiScene::~PmiScene() {
  clear();
}

std::vector<TopLoc_Location> PmiScene::placementsOf(const core::PmiItem& item) const {
  std::vector<TopLoc_Location> placements;
  if (m_index && !item.ownerPart.IsNull()) {
    for (const core::AssemblyNode& node : m_index->nodes()) {
      if (node.prototypeLabel == item.ownerPart) {
        placements.push_back(node.location);
      }
    }
  }
  if (placements.empty()) {
    placements.emplace_back(); // defined at the top level (or owner unknown)
  }
  return placements;
}

void PmiScene::setModel(std::shared_ptr<const core::PmiModel> pmi,
                        std::shared_ptr<const core::AssemblyIndex> index, double devicePixelRatio) {
  clear();
  m_pmi = std::move(pmi);
  m_index = std::move(index);
  if (!m_pmi) {
    return;
  }
  m_entries.resize(m_pmi->items().size());
  m_dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
  size_t graphical = 0;
  for (const core::PmiItem& item : m_pmi->items()) {
    graphical += item.presentation.IsNull() ? 0 : 1;
  }
  m_semanticLabels = graphical * 2 < m_pmi->items().size() || m_pmi->items().empty();
  // Labels sharing an anchor are stacked (view independent); the rest of the decluttering
  // needs the camera and is done by declutter(), which the owner of the view calls.
  std::vector<std::pair<gp_Pnt, int>> anchors;
  for (const core::PmiItem& item : m_pmi->items()) {
    Entry& entry = m_entries[static_cast<size_t>(item.id)];
    for (const TopLoc_Location& placement : placementsOf(item)) {
      OccHandle<PmiPresentation> prs = new PmiPresentation(item, placement, devicePixelRatio);
      m_objectToPmi[prs.get()] = item.id;
      m_context->Display(prs, 0, 0, false);
      entry.placements.push_back(prs);
      for (const PmiPresentation::ArrowSpec& spec : prs->arrows()) {
        const gp_Trsf trsf = placement.Transformation();
        OccHandle<PmiArrow> arrow = new PmiArrow(spec.tip.Transformed(trsf), spec.dir.Transformed(trsf),
                                                 spec.side.Transformed(trsf), devicePixelRatio);
        m_context->Display(arrow, 0, -1, false);
        entry.arrows.push_back(arrow);
      }
      if (prs->hasLabel()) {
        const gp_Pnt anchor = prs->labelAnchor().Transformed(placement.Transformation());
        auto same = std::find_if(anchors.begin(), anchors.end(), [&anchor](const auto& other) {
          return other.first.Distance(anchor) < kSameAnchorDistance;
        });
        if (same == anchors.end()) {
          anchors.emplace_back(anchor, 0);
          same = std::prev(anchors.end());
        }
        OccHandle<PmiLabel> label = new PmiLabel(item, anchor, same->second++, devicePixelRatio);
        m_objectToPmi[label.get()] = item.id;
        m_context->Display(label, 0, 0, false);
        entry.labels.push_back(label);
      }
    }
  }
  if (!m_semanticLabels) {
    for (Entry& entry : m_entries) {
      updateDisplay(entry);
    }
  }
}

// Greedy screen-space layout. Labels are boxes of constant pixel size at a world anchor, so
// whether they overlap depends on the camera; this lays them out for the camera as it is
// now and is meant to run when the view has settled (after fit, saved view, model load),
// never per frame. Each label keeps its own position if free, otherwise takes the nearest
// free slot among a fixed candidate list (above/below, then to the sides), joined to its
// anchor by a connector. Cost: O(n * candidates) with a grid over the placed boxes.
void PmiScene::declutter(const OccHandle<V3d_View>& view) {
  if (view.IsNull() || !m_pmi) {
    return;
  }
  struct Box {
    double x0, y0, x1, y1; // window pixels, y down
  };
  const auto overlap = [](const Box& a, const Box& b) {
    const double w = std::min(a.x1, b.x1) - std::max(a.x0, b.x0);
    const double h = std::min(a.y1, b.y1) - std::max(a.y0, b.y0);
    return w > 0.0 && h > 0.0 ? w * h : 0.0;
  };
  constexpr double kCell = 128.0;
  std::vector<Box> placed;
  std::unordered_map<long long, std::vector<int>> grid;
  const auto cellKey = [](long long cx, long long cy) { return cx * 1000003LL + cy; };
  const auto forCells = [&](const Box& box, const auto& visit) {
    for (long long cx = static_cast<long long>(std::floor(box.x0 / kCell));
         cx <= static_cast<long long>(std::floor(box.x1 / kCell)); ++cx) {
      for (long long cy = static_cast<long long>(std::floor(box.y0 / kCell));
           cy <= static_cast<long long>(std::floor(box.y1 / kCell)); ++cy) {
        visit(cellKey(cx, cy));
      }
    }
  };
  const auto collision = [&](const Box& box) {
    double total = 0.0;
    std::vector<int> seen;
    forCells(box, [&](long long key) {
      const auto bucket = grid.find(key);
      if (bucket == grid.end()) {
        return;
      }
      for (const int index : bucket->second) {
        if (std::find(seen.begin(), seen.end(), index) == seen.end()) {
          seen.push_back(index);
          total += overlap(box, placed[static_cast<size_t>(index)]);
        }
      }
    });
    return total;
  };

  const double pitch = PmiLabel::stackPitch(m_dpr);
  const double gap = std::round(6.0 * m_dpr);
  constexpr int kColumns[] = {0, 1, -1, 2, -2};
  constexpr int kRows[] = {0, -1, 1, -2, 2, -3, 3};
  for (Entry& entry : m_entries) {
    if (!entry.visible) {
      continue;
    }
    for (const OccHandle<PmiLabel>& label : entry.labels) {
      int ax = 0;
      int ay = 0;
      view->Convert(label->anchor().X(), label->anchor().Y(), label->anchor().Z(), ax, ay);
      // The label without offset; local y is up, window y is down.
      const double left = label->boxLeft() - label->offsetX();
      const double centerY = label->centerY() - label->offsetY();
      const double half = label->boxHeight() / 2.0;
      const double width = label->boxWidth();

      Box best{};
      double bestOverlap = -1.0;
      double bestDx = 0.0;
      double bestDy = 0.0;
      for (const int column : kColumns) {
        for (const int row : kRows) {
          const double dx = column * (width + gap);
          const double dy = row * pitch;
          const Box box{ax + left + dx - 1.0, ay - (centerY + dy + half) - 1.0, ax + left + dx + width + 1.0,
                        ay - (centerY + dy - half) + 1.0};
          const double hit = collision(box);
          if (bestOverlap < 0.0 || hit < bestOverlap) {
            bestOverlap = hit;
            best = box;
            bestDx = dx;
            bestDy = dy;
          }
          if (hit == 0.0) {
            break;
          }
        }
        if (bestOverlap == 0.0) {
          break;
        }
      }
      placed.push_back(best);
      const int index = static_cast<int>(placed.size()) - 1;
      forCells(best, [&](long long key) { grid[key].push_back(index); });
      if (std::abs(label->offsetX() - bestDx) > 0.5 || std::abs(label->offsetY() - bestDy) > 0.5) {
        label->setOffset(bestDx, bestDy);
        m_context->Redisplay(label, false, true);
      }
    }
  }
}

void PmiScene::clear() {
  removeTargetHighlight();
  for (Entry& entry : m_entries) {
    for (const auto& prs : entry.placements) {
      m_context->Remove(prs, false);
    }
    for (const auto& label : entry.labels) {
      m_context->Remove(label, false);
    }
    for (const auto& arrow : entry.arrows) {
      m_context->Remove(arrow, false);
    }
  }
  m_entries.clear();
  m_objectToPmi.clear();
  m_highlighted.clear();
  m_pmi.reset();
  m_index.reset();
}

void PmiScene::setVisible(const std::vector<core::PmiId>& ids, bool visible) {
  for (const core::PmiId id : ids) {
    if (id < 0 || id >= static_cast<core::PmiId>(m_entries.size())) {
      continue;
    }
    Entry& entry = m_entries[static_cast<size_t>(id)];
    if (entry.visible == visible) {
      continue;
    }
    entry.visible = visible;
    updateDisplay(entry);
  }
}

// Shows or hides the objects of an item: everything follows the item's visibility, and the
// semantic-only parts (leaders, dimension geometry, arrows, labels) also the semantic switch.
void PmiScene::updateDisplay(Entry& entry) {
  const auto apply = [this](const OccHandle<AIS_InteractiveObject>& object, bool wanted, int selectionMode) {
    const bool shown = m_context->IsDisplayed(object);
    if (wanted && !shown) {
      m_context->Display(object, 0, selectionMode, false);
    } else if (!wanted && shown) {
      m_context->Erase(object, false);
    }
  };
  for (const auto& prs : entry.placements) {
    apply(prs, entry.visible && (m_semanticLabels || !prs->hasLabel()), 0);
  }
  for (const auto& label : entry.labels) {
    apply(label, entry.visible && m_semanticLabels, 0);
  }
  for (const auto& arrow : entry.arrows) {
    apply(arrow, entry.visible && m_semanticLabels, -1);
  }
}

void PmiScene::setSemanticLabelsVisible(bool visible) {
  if (m_semanticLabels == visible) {
    return;
  }
  m_semanticLabels = visible;
  for (Entry& entry : m_entries) {
    updateDisplay(entry);
  }
}

void PmiScene::setOnlyVisible(const std::vector<core::PmiId>& ids) {
  std::vector<core::PmiId> hide;
  for (size_t i = 0; i < m_entries.size(); ++i) {
    if (std::find(ids.begin(), ids.end(), static_cast<core::PmiId>(i)) == ids.end()) {
      hide.push_back(static_cast<core::PmiId>(i));
    }
  }
  setVisible(hide, false);
  setVisible(ids, true);
}

bool PmiScene::isVisible(core::PmiId id) const {
  return id >= 0 && id < static_cast<core::PmiId>(m_entries.size()) &&
         m_entries[static_cast<size_t>(id)].visible;
}

std::vector<core::PmiId> PmiScene::visibleIds() const {
  std::vector<core::PmiId> ids;
  for (size_t i = 0; i < m_entries.size(); ++i) {
    if (m_entries[i].visible) {
      ids.push_back(static_cast<core::PmiId>(i));
    }
  }
  return ids;
}

core::PmiId PmiScene::pmiOf(const OccHandle<AIS_InteractiveObject>& object) const {
  const auto it = m_objectToPmi.find(object.get());
  return it == m_objectToPmi.end() ? core::kNoPmi : it->second;
}

std::vector<core::PmiId> PmiScene::selectedPmi() const {
  std::vector<core::PmiId> ids;
  for (m_context->InitSelected(); m_context->MoreSelected(); m_context->NextSelected()) {
    const core::PmiId id = pmiOf(m_context->SelectedInteractive());
    if (id != core::kNoPmi && std::find(ids.begin(), ids.end(), id) == ids.end()) {
      ids.push_back(id);
    }
  }
  return ids;
}

void PmiScene::removeTargetHighlight() {
  if (!m_targetHighlight.IsNull()) {
    m_context->Remove(m_targetHighlight, false);
    m_targetHighlight.Nullify();
  }
}

void PmiScene::highlight(const std::vector<core::PmiId>& ids) {
  removeTargetHighlight();
  m_highlighted = ids;
  m_context->ClearSelected(false);
  if (!m_pmi) {
    return;
  }

  TopoDS_Compound targets;
  BRep_Builder builder;
  builder.MakeCompound(targets);
  bool any = false;
  for (const core::PmiId id : ids) {
    if (id < 0 || id >= static_cast<core::PmiId>(m_entries.size())) {
      continue;
    }
    const core::PmiItem& item = m_pmi->item(id);
    const std::vector<TopLoc_Location> placements = placementsOf(item);
    for (const auto* shapes : {&item.targets, &item.secondTargets}) {
      for (const TopoDS_Shape& shape : *shapes) {
        for (const TopLoc_Location& placement : placements) {
          if (!shape.IsNull()) {
            builder.Add(targets, shape.Moved(placement));
            any = true;
          }
        }
      }
    }
    const Entry& entry = m_entries[static_cast<size_t>(id)];
    for (const auto& prs : entry.placements) {
      if (m_context->IsDisplayed(prs)) {
        m_context->AddOrRemoveSelected(prs, false);
      }
    }
    for (const auto& label : entry.labels) {
      if (m_context->IsDisplayed(label)) {
        m_context->AddOrRemoveSelected(label, false);
      }
    }
  }
  if (!any) {
    return;
  }
  OccHandle<AIS_Shape> shape = new AIS_Shape(targets);
  shape->SetColor(kTargetColor);
  shape->SetDisplayMode(AIS_Shaded);
  shape->SetWidth(3.0);
  // Drawn over the coplanar part faces it marks.
  shape->Attributes()->SetupOwnShadingAspect();
  const OccHandle<Graphic3d_AspectFillArea3d>& fill = shape->Attributes()->ShadingAspect()->Aspect();
  fill->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit); // flat color, readable from any angle
  fill->SetInteriorColor(kTargetColor);
  fill->SetPolygonOffsets(Aspect_POM_Fill, -2.0f, -2.0f);
  shape->SetZLayer(Graphic3d_ZLayerId_Top);
  m_targetHighlight = shape;
  m_context->Display(shape, AIS_Shaded, -1, false); // not selectable
}

// Places the camera from the view's STEP camera data (eye, view plane, window), all in mm.
// Returns false when that data is missing so the caller can fit the model instead.
static bool placeSavedViewCamera(const core::PmiSavedView& view, Graphic3d_Camera& camera) {
  if (!view.eye || !view.windowWidthMm || !view.windowHeightMm || !view.viewPlaneDistanceMm) {
    return false;
  }
  // Camera frame: Z viewing direction, Y up, X = Y x Z (so that Z = X x Y).
  const gp_Dir z = view.viewDirection;
  const gp_Dir x = view.upDirection.Crossed(z);
  const gp_Dir y = z.Crossed(x);
  const gp_Vec zv(z);
  const gp_Vec xv(x);
  const gp_Vec yv(y);
  const gp_Pnt origin = *view.eye;
  const double planeDistance = *view.viewPlaneDistanceMm;
  const gp_Pnt center =
      origin.Translated(xv * view.windowCenterX + yv * view.windowCenterY + zv * planeDistance);
  const double aspect = camera.Aspect() > 1e-6 ? camera.Aspect() : 1.0;
  // The window must fit entirely, whatever the viewport's aspect ratio.
  const double extent = std::max(*view.windowHeightMm, *view.windowWidthMm / aspect);

  if (!view.perspective) {
    // Parallel: only the direction and the window matter; the eye sits on the view plane's
    // normal through the window centre.
    camera.SetProjectionType(Graphic3d_Camera::Projection_Orthographic);
    camera.SetEyeAndCenter(center.Translated(-zv * planeDistance), center);
    camera.SetUp(y);
    camera.OrthogonalizeUp();
    camera.SetScale(extent);
    return true;
  }
  // Central: rays converge at the projection point (given in the camera frame).
  const gp_Pnt eye = origin.Translated(xv * view.projectionPoint.X() + yv * view.projectionPoint.Y() +
                                       zv * view.projectionPoint.Z());
  const double distance = gp_Vec(eye, center).Dot(zv);
  if (distance < 1e-6) {
    return false;
  }
  constexpr double kRadToDeg = 57.29577951308232;
  const double fov = std::clamp(2.0 * std::atan(extent * 0.5 / distance) * kRadToDeg, 1.0, 170.0);
  camera.SetProjectionType(Graphic3d_Camera::Projection_Perspective);
  camera.SetEyeAndCenter(eye, center);
  camera.SetUp(y);
  camera.OrthogonalizeUp();
  camera.SetFOVy(fov);
  return true;
}

void PmiScene::applyView(const core::PmiSavedView& view, const OccHandle<V3d_View>& v3dView) {
  if (m_pmi) {
    setOnlyVisible(view.pmi);
  }

  // Camera. With the STEP camera (location, view window) the saved view is reproduced: the
  // camera frame's Z axis is the viewing direction, the view plane lies viewPlaneDistance
  // along it and the window on that plane sets the visible extent. Without it (views built
  // directly in XCAF) only the orientation is known, so the model is fitted instead.
  const OccHandle<Graphic3d_Camera>& camera = v3dView->Camera();
  if (placeSavedViewCamera(view, *camera)) {
    v3dView->ZFitAll();
  } else {
    camera->SetProjectionType(view.perspective ? Graphic3d_Camera::Projection_Perspective
                                               : Graphic3d_Camera::Projection_Orthographic);
    camera->SetDirection(view.viewDirection);
    camera->SetUp(view.upDirection);
    camera->OrthogonalizeUp();
    v3dView->FitAll(0.05, false);
    v3dView->ZFitAll();
  }

  // Clipping planes.
  clearViewClipping(v3dView);
  for (const gp_Pln& plane : view.clippingPlanes) {
    OccHandle<Graphic3d_ClipPlane> clip = new Graphic3d_ClipPlane(plane);
    clip->SetCapping(false);
    v3dView->AddClipPlane(clip);
    m_clipPlanes.push_back(clip);
  }
}

void PmiScene::clearViewClipping(const OccHandle<V3d_View>& v3dView) {
  for (const auto& clip : m_clipPlanes) {
    v3dView->RemoveClipPlane(clip);
  }
  m_clipPlanes.clear();
}

int PmiScene::presentationCount() const {
  int count = 0;
  for (const Entry& entry : m_entries) {
    count += static_cast<int>(entry.placements.size());
  }
  return count;
}

const std::vector<OccHandle<PmiLabel>>& PmiScene::labelsOf(core::PmiId id) const {
  static const std::vector<OccHandle<PmiLabel>> none;
  return id >= 0 && id < static_cast<core::PmiId>(m_entries.size())
             ? m_entries[static_cast<size_t>(id)].labels
             : none;
}

int PmiScene::arrowCount() const {
  int count = 0;
  for (const Entry& entry : m_entries) {
    count += static_cast<int>(entry.arrows.size());
  }
  return count;
}

} // namespace sc::render
