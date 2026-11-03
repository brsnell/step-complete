#include "core/Measure.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Bnd_OBB.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
#include <GProp_PrincipalProps.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Lin.hxx>

#include "core/OccCompat.h"

#include <algorithm>
#include <cmath>

namespace sc::core::measure {

namespace {

constexpr double kPi = 3.14159265358979323846;
// Directions closer than this (radians) count as parallel.
constexpr double kParallelTolerance = 1e-6;

double maxTolerance(const TopoDS_Shape& shape) {
  double tolerance = 0.0;
  if (shape.IsNull()) {
    return tolerance;
  }
  for (TopExp_Explorer it(shape, TopAbs_VERTEX); it.More(); it.Next()) {
    tolerance = std::max(tolerance, BRep_Tool::Tolerance(TopoDS::Vertex(it.Current())));
  }
  for (TopExp_Explorer it(shape, TopAbs_EDGE); it.More(); it.Next()) {
    tolerance = std::max(tolerance, BRep_Tool::Tolerance(TopoDS::Edge(it.Current())));
  }
  for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
    tolerance = std::max(tolerance, BRep_Tool::Tolerance(TopoDS::Face(it.Current())));
  }
  return tolerance;
}

CurveKind curveKind(GeomAbs_CurveType type) {
  switch (type) {
  case GeomAbs_Line:
    return CurveKind::Line;
  case GeomAbs_Circle:
    return CurveKind::Circle;
  case GeomAbs_Ellipse:
    return CurveKind::Ellipse;
  case GeomAbs_Hyperbola:
    return CurveKind::Hyperbola;
  case GeomAbs_Parabola:
    return CurveKind::Parabola;
  case GeomAbs_BezierCurve:
    return CurveKind::Bezier;
  case GeomAbs_BSplineCurve:
    return CurveKind::BSpline;
  case GeomAbs_OffsetCurve:
    return CurveKind::Offset;
  default:
    return CurveKind::Other;
  }
}

SurfaceKind surfaceKind(GeomAbs_SurfaceType type) {
  switch (type) {
  case GeomAbs_Plane:
    return SurfaceKind::Plane;
  case GeomAbs_Cylinder:
    return SurfaceKind::Cylinder;
  case GeomAbs_Cone:
    return SurfaceKind::Cone;
  case GeomAbs_Sphere:
    return SurfaceKind::Sphere;
  case GeomAbs_Torus:
    return SurfaceKind::Torus;
  case GeomAbs_BezierSurface:
    return SurfaceKind::Bezier;
  case GeomAbs_BSplineSurface:
    return SurfaceKind::BSpline;
  case GeomAbs_SurfaceOfRevolution:
    return SurfaceKind::Revolution;
  case GeomAbs_SurfaceOfExtrusion:
    return SurfaceKind::Extrusion;
  case GeomAbs_OffsetSurface:
    return SurfaceKind::Offset;
  default:
    return SurfaceKind::Other;
  }
}

EdgeInfo edgeInfo(const TopoDS_Edge& edge) {
  EdgeInfo info;
  if (BRep_Tool::Degenerated(edge)) {
    TopoDS_Vertex v1, v2;
    TopExp::Vertices(edge, v1, v2, true);
    if (!v1.IsNull()) {
      info.start = info.end = info.midpoint = BRep_Tool::Pnt(v1);
    }
    info.closed = true;
    return info;
  }

  const BRepAdaptor_Curve curve(edge); // includes the edge's location
  info.curve = curveKind(curve.GetType());
  const double first = curve.FirstParameter();
  const double last = curve.LastParameter();
  info.length = GCPnts_AbscissaPoint::Length(curve);

  // Respect edge orientation for start/end.
  info.start = curve.Value(first);
  info.end = curve.Value(last);
  if (edge.Orientation() == TopAbs_REVERSED) {
    std::swap(info.start, info.end);
  }
  const GCPnts_AbscissaPoint middle(curve, info.length / 2.0, first);
  info.midpoint = curve.Value(middle.IsDone() ? middle.Parameter() : (first + last) / 2.0);
  info.closed = BRep_Tool::IsClosed(edge) || info.start.Distance(info.end) <= BRep_Tool::Tolerance(edge);

  switch (curve.GetType()) {
  case GeomAbs_Line:
    if (info.start.Distance(info.end) > Precision::Confusion()) {
      info.direction = gp_Dir(gp_Vec(info.start, info.end));
    }
    break;
  case GeomAbs_Circle: {
    const gp_Circ circle = curve.Circle();
    info.radius = circle.Radius();
    info.axis = circle.Axis();
    info.sweepAngle = std::min(last - first, 2.0 * kPi);
    break;
  }
  case GeomAbs_Ellipse: {
    const gp_Elips ellipse = curve.Ellipse();
    info.radius = ellipse.MajorRadius();
    info.minorRadius = ellipse.MinorRadius();
    info.axis = ellipse.Axis();
    break;
  }
  default:
    break;
  }
  return info;
}

FaceInfo faceInfo(const TopoDS_Face& face) {
  FaceInfo info;
  const BRepAdaptor_Surface surface(face); // includes the face's location
  info.surface = surfaceKind(surface.GetType());

  GProp_GProps props;
  BRepGProp::SurfaceProperties(face, props);
  info.area = props.Mass();
  info.centroid = props.CentreOfMass();

  switch (surface.GetType()) {
  case GeomAbs_Plane: {
    gp_Dir normal = surface.Plane().Axis().Direction();
    if (face.Orientation() == TopAbs_REVERSED) {
      normal.Reverse();
    }
    info.normal = normal;
    break;
  }
  case GeomAbs_Cylinder:
    info.axis = surface.Cylinder().Axis();
    info.radius = surface.Cylinder().Radius();
    break;
  case GeomAbs_Cone:
    info.axis = surface.Cone().Axis();
    info.radius = surface.Cone().RefRadius();
    info.halfAngle = std::abs(surface.Cone().SemiAngle());
    break;
  case GeomAbs_Sphere:
    info.axis = surface.Sphere().Position().Axis();
    info.radius = surface.Sphere().Radius();
    break;
  case GeomAbs_Torus:
    info.axis = surface.Torus().Axis();
    info.radius = surface.Torus().MajorRadius();
    info.minorRadius = surface.Torus().MinorRadius();
    break;
  case GeomAbs_SurfaceOfRevolution:
    info.axis = surface.AxeOfRevolution();
    break;
  default:
    break;
  }
  return info;
}

// A feature's "center" for center-to-center distances: a point (circle,
// ellipse, sphere, torus, vertex) or a line (cylinder, cone axis).
struct Center {
  std::optional<gp_Pnt> point;
  std::optional<gp_Lin> line;
};

Center centerOf(const EntityInfo& info) {
  Center center;
  if (info.kind == EntityKind::Vertex) {
    center.point = info.point;
  } else if (info.edge && info.edge->axis &&
             (info.edge->curve == CurveKind::Circle || info.edge->curve == CurveKind::Ellipse)) {
    center.point = info.edge->axis->Location();
  } else if (info.face && info.face->axis) {
    switch (info.face->surface) {
    case SurfaceKind::Sphere:
    case SurfaceKind::Torus:
      center.point = info.face->axis->Location();
      break;
    case SurfaceKind::Cylinder:
    case SurfaceKind::Cone:
      center.line = gp_Lin(*info.face->axis);
      break;
    default:
      break;
    }
  }
  return center;
}

gp_Pnt project(const gp_Lin& line, const gp_Pnt& point) {
  const gp_Vec offset(line.Location(), point);
  const double along = offset.Dot(gp_Vec(line.Direction()));
  return line.Location().Translated(gp_Vec(line.Direction()) * along);
}

void addCenterDistance(const EntityInfo& a, const EntityInfo& b, DistanceResult& result) {
  const Center ca = centerOf(a);
  const Center cb = centerOf(b);
  if (ca.point && cb.point) {
    result.center1 = *ca.point;
    result.center2 = *cb.point;
  } else if (ca.point && cb.line) {
    result.center1 = *ca.point;
    result.center2 = project(*cb.line, *ca.point);
  } else if (ca.line && cb.point) {
    result.center1 = project(*ca.line, *cb.point);
    result.center2 = *cb.point;
  } else if (ca.line && cb.line &&
             ca.line->Direction().IsParallel(cb.line->Direction(), kParallelTolerance)) {
    result.center1 = ca.line->Location();
    result.center2 = project(*cb.line, ca.line->Location());
  }
  if (result.center1 && result.center2) {
    result.centerDistance = result.center1->Distance(*result.center2);
  }
}

void addParallelDistance(const EntityInfo& a, const EntityInfo& b, DistanceResult& result) {
  if (a.face && b.face && a.face->normal && b.face->normal &&
      a.face->normal->IsParallel(*b.face->normal, kParallelTolerance)) {
    // Any point on each plane works; the face centroids lie on their planes.
    result.parallelDistance =
        std::abs(gp_Vec(a.face->centroid, b.face->centroid).Dot(gp_Vec(*a.face->normal)));
  } else if (a.edge && b.edge && a.edge->direction && b.edge->direction &&
             a.edge->direction->IsParallel(*b.edge->direction, kParallelTolerance)) {
    result.parallelDistance = gp_Lin(a.edge->start, *a.edge->direction).Distance(b.edge->start);
  }
}

// Direction carried by an entity for angle measurement.
struct Direction {
  gp_Dir dir;
  std::optional<gp_Lin> line; // when the direction has a position (edges, axes)
  bool straightEdge = false;
  gp_Pnt start, end; // straight edges only
};

std::optional<Direction> directionOf(const EntityInfo& info) {
  Direction d;
  if (info.edge && info.edge->direction) {
    d.dir = *info.edge->direction;
    d.line = gp_Lin(info.edge->start, d.dir);
    d.straightEdge = true;
    d.start = info.edge->start;
    d.end = info.edge->end;
    return d;
  }
  if (info.edge && info.edge->axis) { // circles, ellipses: their normal axis
    d.dir = info.edge->axis->Direction();
    d.line = gp_Lin(*info.edge->axis);
    return d;
  }
  if (info.face && info.face->normal) {
    d.dir = *info.face->normal;
    return d;
  }
  if (info.face && info.face->axis &&
      (info.face->surface == SurfaceKind::Cylinder || info.face->surface == SurfaceKind::Cone ||
       info.face->surface == SurfaceKind::Torus || info.face->surface == SurfaceKind::Revolution)) {
    d.dir = info.face->axis->Direction();
    d.line = gp_Lin(*info.face->axis);
    return d;
  }
  return std::nullopt;
}

// Intersection of two non-parallel lines, if they meet within tolerance.
std::optional<gp_Pnt> intersect(const gp_Lin& l1, const gp_Lin& l2, double tolerance) {
  const gp_Vec d1(l1.Direction());
  const gp_Vec d2(l2.Direction());
  const gp_Vec w(l2.Location(), l1.Location());
  const double b = d1.Dot(d2);
  const double denom = 1.0 - b * b;
  if (denom < 1e-12) {
    return std::nullopt;
  }
  const double d = d1.Dot(w);
  const double e = d2.Dot(w);
  const double s = (b * e - d) / denom;
  const double t = (e - b * d) / denom;
  const gp_Pnt p1 = l1.Location().Translated(d1 * s);
  const gp_Pnt p2 = l2.Location().Translated(d2 * t);
  if (p1.Distance(p2) > tolerance) {
    return std::nullopt;
  }
  return gp_Pnt((p1.XYZ() + p2.XYZ()) / 2.0);
}

TopoDS_Compound makeCompound(const std::vector<TopoDS_Shape>& shapes) {
  TopoDS_Compound compound;
  BRep_Builder builder;
  builder.MakeCompound(compound);
  for (const TopoDS_Shape& shape : shapes) {
    if (!shape.IsNull()) {
      builder.Add(compound, shape);
    }
  }
  return compound;
}

} // namespace

TopoDS_Shape toWorld(const TopoDS_Shape& shape, const TopLoc_Location& instanceLocation) {
  return shape.IsNull() ? shape : shape.Moved(instanceLocation);
}

const char* toString(EntityKind kind) {
  switch (kind) {
  case EntityKind::Vertex:
    return "Vertex";
  case EntityKind::Edge:
    return "Edge";
  case EntityKind::Face:
    return "Face";
  case EntityKind::Solid:
    return "Solid";
  case EntityKind::Shell:
    return "Shell";
  case EntityKind::Compound:
    return "Compound";
  case EntityKind::Other:
    break;
  }
  return "Other";
}

const char* toString(CurveKind kind) {
  switch (kind) {
  case CurveKind::Line:
    return "Line";
  case CurveKind::Circle:
    return "Circle";
  case CurveKind::Ellipse:
    return "Ellipse";
  case CurveKind::Hyperbola:
    return "Hyperbola";
  case CurveKind::Parabola:
    return "Parabola";
  case CurveKind::Bezier:
    return "Bezier";
  case CurveKind::BSpline:
    return "B-spline";
  case CurveKind::Offset:
    return "Offset";
  case CurveKind::Other:
    break;
  }
  return "Other";
}

const char* toString(SurfaceKind kind) {
  switch (kind) {
  case SurfaceKind::Plane:
    return "Plane";
  case SurfaceKind::Cylinder:
    return "Cylinder";
  case SurfaceKind::Cone:
    return "Cone";
  case SurfaceKind::Sphere:
    return "Sphere";
  case SurfaceKind::Torus:
    return "Torus";
  case SurfaceKind::Bezier:
    return "Bezier";
  case SurfaceKind::BSpline:
    return "B-spline";
  case SurfaceKind::Revolution:
    return "Revolution";
  case SurfaceKind::Extrusion:
    return "Extrusion";
  case SurfaceKind::Offset:
    return "Offset";
  case SurfaceKind::Other:
    break;
  }
  return "Other";
}

EntityInfo entityInfo(const TopoDS_Shape& shape) {
  EntityInfo info;
  if (shape.IsNull()) {
    return info;
  }
  info.tolerance = maxTolerance(shape);
  switch (shape.ShapeType()) {
  case TopAbs_VERTEX:
    info.kind = EntityKind::Vertex;
    info.point = BRep_Tool::Pnt(TopoDS::Vertex(shape));
    break;
  case TopAbs_EDGE:
    info.kind = EntityKind::Edge;
    info.edge = edgeInfo(TopoDS::Edge(shape));
    break;
  case TopAbs_FACE:
    info.kind = EntityKind::Face;
    info.face = faceInfo(TopoDS::Face(shape));
    break;
  case TopAbs_SOLID:
  case TopAbs_COMPSOLID:
  case TopAbs_SHELL:
  case TopAbs_COMPOUND: {
    info.kind = shape.ShapeType() == TopAbs_SHELL      ? EntityKind::Shell
                : shape.ShapeType() == TopAbs_COMPOUND ? EntityKind::Compound
                                                       : EntityKind::Solid;
    const MassProperties mass = massProperties({shape});
    if (mass.ok) {
      info.solid = SolidInfo{mass.volume, mass.area, mass.centroid};
    }
    break;
  }
  default:
    break;
  }
  return info;
}

DistanceResult distance(const gp_Pnt& a, const gp_Pnt& b) {
  DistanceResult result;
  result.ok = true;
  result.minimum = a.Distance(b);
  result.point1 = a;
  result.point2 = b;
  result.delta = gp_Vec(a, b);
  return result;
}

DistanceResult distance(const TopoDS_Shape& a, const TopoDS_Shape& b) {
  DistanceResult result;
  if (a.IsNull() || b.IsNull()) {
    result.error = "Nothing to measure.";
    return result;
  }
  try {
    BRepExtrema_DistShapeShape extrema(a, b, Extrema_ExtFlag_MIN);
    if (!extrema.IsDone() || extrema.NbSolution() < 1) {
      result.error = "The minimum distance could not be computed.";
      return result;
    }
    result = distance(extrema.PointOnShape1(1), extrema.PointOnShape2(1));
    result.minimum = extrema.Value();
  } catch (const Standard_Failure& failure) {
    result.error = occ::errorMessage(failure);
    return result;
  }

  const EntityInfo infoA = entityInfo(a);
  const EntityInfo infoB = entityInfo(b);
  result.tolerance = std::max(infoA.tolerance, infoB.tolerance);
  addCenterDistance(infoA, infoB, result);
  addParallelDistance(infoA, infoB, result);
  return result;
}

AngleResult angle(const TopoDS_Shape& a, const TopoDS_Shape& b) {
  AngleResult result;
  if (a.IsNull() || b.IsNull()) {
    result.error = "Nothing to measure.";
    return result;
  }
  const EntityInfo infoA = entityInfo(a);
  const EntityInfo infoB = entityInfo(b);
  std::optional<Direction> da = directionOf(infoA);
  std::optional<Direction> db = directionOf(infoB);
  if (!da || !db) {
    result.error = "Pick straight edges, planar faces, or features with an axis.";
    return result;
  }
  const double tolerance = std::max({infoA.tolerance, infoB.tolerance, Precision::Confusion()});

  // Two straight edges meeting at a vertex: interior angle seen from the shared point.
  if (da->straightEdge && db->straightEdge) {
    const gp_Pnt endsA[2] = {da->start, da->end};
    const gp_Pnt endsB[2] = {db->start, db->end};
    for (int i = 0; i < 2 && !result.apex; ++i) {
      for (int j = 0; j < 2 && !result.apex; ++j) {
        if (endsA[i].Distance(endsB[j]) <= tolerance) {
          result.apex = endsA[i];
          result.direction1 = gp_Dir(gp_Vec(endsA[i], endsA[1 - i]));
          result.direction2 = gp_Dir(gp_Vec(endsB[j], endsB[1 - j]));
        }
      }
    }
    if (result.apex) {
      result.angle = result.direction1.Angle(result.direction2);
      result.supplement = kPi - result.angle;
      result.ok = true;
      return result;
    }
  }

  result.direction1 = da->dir;
  result.direction2 = db->dir;
  double theta = da->dir.Angle(db->dir);
  if (theta > kPi / 2.0) {
    theta = kPi - theta;
    result.direction2.Reverse();
  }
  result.angle = theta;
  result.supplement = kPi - theta;
  if (da->line && db->line) {
    result.apex = intersect(*da->line, *db->line, tolerance);
  }
  result.ok = true;
  return result;
}

std::optional<RadiusResult> radius(const TopoDS_Shape& shape) {
  const EntityInfo info = entityInfo(shape);
  RadiusResult result;
  if (info.edge && info.edge->curve == CurveKind::Circle && info.edge->radius && info.edge->axis) {
    result.radius = *info.edge->radius;
    result.axis = *info.edge->axis;
    result.center = info.edge->axis->Location();
  } else if (info.face && info.face->radius && info.face->axis &&
             (info.face->surface == SurfaceKind::Cylinder || info.face->surface == SurfaceKind::Sphere ||
              info.face->surface == SurfaceKind::Torus)) {
    result.radius = *info.face->radius;
    result.axis = *info.face->axis;
    result.isSphere = info.face->surface == SurfaceKind::Sphere;
    // For a cylinder, place the center level with the face's centroid so the
    // witness geometry sits on the picked face rather than at the axis origin.
    result.center = info.face->surface == SurfaceKind::Cylinder
                        ? project(gp_Lin(result.axis), info.face->centroid)
                        : result.axis.Location();
  } else {
    return std::nullopt;
  }
  result.diameter = 2.0 * result.radius;
  return result;
}

MassProperties massProperties(const std::vector<TopoDS_Shape>& shapes) {
  MassProperties result;
  const TopoDS_Compound all = makeCompound(shapes);

  std::vector<TopoDS_Shape> solids;
  for (TopExp_Explorer it(all, TopAbs_SOLID); it.More(); it.Next()) {
    solids.push_back(it.Current());
  }
  result.solidCount = static_cast<int>(solids.size());
  result.hasVolume = !solids.empty();

  try {
    GProp_GProps surfaceProps;
    BRepGProp::SurfaceProperties(all, surfaceProps);
    result.area = surfaceProps.Mass();

    GProp_GProps props = surfaceProps;
    if (result.hasVolume) {
      props = GProp_GProps();
      BRepGProp::VolumeProperties(makeCompound(solids), props);
      result.volume = std::abs(props.Mass());
    }
    if (std::abs(props.Mass()) <= Precision::Confusion()) {
      return result;
    }
    result.centroid = props.CentreOfMass();
    result.inertia = props.MatrixOfInertia();
    const GProp_PrincipalProps principal = props.PrincipalProperties();
    principal.Moments(result.principalMoments[0], result.principalMoments[1], result.principalMoments[2]);
    result.principalAxes = {gp_Dir(principal.FirstAxisOfInertia()), gp_Dir(principal.SecondAxisOfInertia()),
                            gp_Dir(principal.ThirdAxisOfInertia())};

    Bnd_Box box;
    BRepBndLib::AddOptimal(all, box, false, false);
    if (box.IsVoid()) {
      return result;
    }
    result.boxMin = box.CornerMin();
    result.boxMax = box.CornerMax();

    // The oriented box may use the display mesh when present (much faster on
    // curved parts); it is a visual aid, not a measurement.
    Bnd_OBB obb;
    BRepBndLib::AddOBB(all, obb, true, true, false);
    if (!obb.IsVoid()) {
      result.orientedBox.center = gp_Pnt(obb.Center());
      result.orientedBox.axes = {gp_Dir(obb.XDirection()), gp_Dir(obb.YDirection()),
                                 gp_Dir(obb.ZDirection())};
      result.orientedBox.halfSizes = {obb.XHSize(), obb.YHSize(), obb.ZHSize()};
    }
  } catch (const Standard_Failure&) {
    return result;
  }
  result.ok = true;
  return result;
}

} // namespace sc::core::measure
