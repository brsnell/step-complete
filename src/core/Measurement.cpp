#include "core/Measurement.h"

#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Tool.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <cmath>

namespace sc::core {

namespace {

using measure::CurveKind;
using measure::EntityInfo;
using measure::SurfaceKind;

constexpr double kPi = 3.14159265358979323846;

std::string point(const gp_Pnt& p, const MeasureDisplayOptions& o) {
  return formatNumber(fromMillimeters(p.X(), o.lengthUnit), o.decimals) + ", " +
         formatNumber(fromMillimeters(p.Y(), o.lengthUnit), o.decimals) + ", " +
         formatNumber(fromMillimeters(p.Z(), o.lengthUnit), o.decimals) + " " +
         std::string(symbol(o.lengthUnit));
}

std::string direction(const gp_Dir& d) {
  return formatNumber(d.X(), 4) + ", " + formatNumber(d.Y(), 4) + ", " + formatNumber(d.Z(), 4);
}

std::string length(double mm, const MeasureDisplayOptions& o) {
  return formatLength(mm, o.lengthUnit, o.decimals);
}

// Model tolerances are often far below the display precision; say so instead
// of printing a misleading "0.000".
std::string tolerance(double mm, const MeasureDisplayOptions& o) {
  const double shown = fromMillimeters(mm, o.lengthUnit);
  const double resolution = std::pow(10.0, -std::clamp(o.decimals, 0, 12));
  if (shown < resolution / 2.0) {
    return "< " + formatLength(toMillimeters(resolution, o.lengthUnit), o.lengthUnit, o.decimals);
  }
  return length(mm, o);
}

std::string angleText(double radians, const MeasureDisplayOptions& o) {
  return formatAngle(radians, o.angleUnit, o.angleDecimals);
}

gp_Pnt midpoint(const gp_Pnt& a, const gp_Pnt& b) {
  return gp_Pnt((a.XYZ() + b.XYZ()) / 2.0);
}

// Closest point on a shape to a point (exact B-rep).
gp_Pnt projectOnto(const TopoDS_Shape& shape, const gp_Pnt& p) {
  try {
    BRepExtrema_DistShapeShape extrema(BRepBuilderAPI_MakeVertex(p).Vertex(), shape, Extrema_ExtFlag_MIN);
    if (extrema.IsDone() && extrema.NbSolution() > 0) {
      return extrema.PointOnShape2(1);
    }
  } catch (const Standard_Failure&) {
  }
  return p;
}

// Any unit vector perpendicular to 'axis', preferring the direction towards 'toward'.
gp_Dir radialDirection(const gp_Ax1& axis, const gp_Pnt& toward) {
  const gp_Vec n(axis.Direction());
  gp_Vec v(axis.Location(), toward);
  v -= n * v.Dot(n);
  if (v.Magnitude() > Precision::Confusion()) {
    return gp_Dir(v);
  }
  const gp_Vec helper = std::abs(n.X()) < 0.9 ? gp_Vec(1, 0, 0) : gp_Vec(0, 1, 0);
  return gp_Dir(n.Crossed(helper));
}

std::vector<gp_Pnt> circle(const gp_Pnt& center, const gp_Dir& normal, const gp_Dir& start, double r,
                           int n = 64) {
  const gp_Vec u(start);
  const gp_Vec w = gp_Vec(normal).Crossed(u);
  std::vector<gp_Pnt> points;
  for (int i = 0; i <= n; ++i) {
    const double t = 2.0 * kPi * i / n;
    points.push_back(center.Translated(u * (r * std::cos(t)) + w * (r * std::sin(t))));
  }
  return points;
}

void entityInfoReport(const Pick& pick, const MeasureDisplayOptions& o, Measurement& m) {
  const EntityInfo info = measure::entityInfo(pick.shape);
  auto add = [&m](std::string key, std::string value) {
    m.details.emplace_back(std::move(key), std::move(value));
  };
  Witness& w = m.witness;
  w.labelAnchor = pick.point;

  switch (info.kind) {
  case measure::EntityKind::Vertex:
    m.headline = point(info.point, o);
    add("Position", m.headline);
    w.markers.push_back(info.point);
    w.labelAnchor = info.point;
    break;
  case measure::EntityKind::Edge: {
    const measure::EdgeInfo& e = *info.edge;
    m.headline = length(e.length, o);
    add("Type", measure::toString(e.curve));
    add("Length", m.headline);
    if (e.radius) {
      add(e.curve == CurveKind::Ellipse ? "Major radius" : "Radius", length(*e.radius, o));
      if (e.curve == CurveKind::Circle) {
        add("Diameter", length(2.0 * *e.radius, o));
      }
    }
    if (e.minorRadius) {
      add("Minor radius", length(*e.minorRadius, o));
    }
    if (e.axis) {
      add("Center", point(e.axis->Location(), o));
      add("Axis", direction(e.axis->Direction()));
    }
    if (e.sweepAngle && !e.closed) {
      add("Arc angle", angleText(*e.sweepAngle, o));
    }
    if (e.direction) {
      add("Direction", direction(*e.direction));
    }
    if (!e.closed) {
      add("Start", point(e.start, o));
      add("End", point(e.end, o));
      w.markers.push_back(e.start);
      w.markers.push_back(e.end);
    }
    add("Midpoint", point(e.midpoint, o));
    w.labelAnchor = e.midpoint;
    break;
  }
  case measure::EntityKind::Face: {
    const measure::FaceInfo& f = *info.face;
    m.headline = formatArea(f.area, o.lengthUnit, o.decimals);
    add("Type", measure::toString(f.surface));
    add("Area", m.headline);
    if (f.normal) {
      add("Normal", direction(*f.normal));
    }
    if (f.radius) {
      add(f.surface == SurfaceKind::Torus  ? "Major radius"
          : f.surface == SurfaceKind::Cone ? "Reference radius"
                                           : "Radius",
          length(*f.radius, o));
      if (f.surface == SurfaceKind::Cylinder || f.surface == SurfaceKind::Sphere) {
        add("Diameter", length(2.0 * *f.radius, o));
      }
    }
    if (f.minorRadius) {
      add("Minor radius", length(*f.minorRadius, o));
    }
    if (f.halfAngle) {
      add("Half angle", angleText(*f.halfAngle, o));
    }
    if (f.axis) {
      add(f.surface == SurfaceKind::Sphere ? "Center" : "Axis point", point(f.axis->Location(), o));
      if (f.surface != SurfaceKind::Sphere) {
        add("Axis", direction(f.axis->Direction()));
      }
    }
    add("Centroid", point(f.centroid, o));
    w.markers.push_back(pick.point);
    break;
  }
  default:
    m.error = "Pick a vertex, edge or face.";
    return;
  }
  add("Tolerance", tolerance(info.tolerance, o));
  w.label = m.headline;
  m.ok = true;
}

void distanceReport(const measure::DistanceResult& r, const MeasureDisplayOptions& o, Measurement& m) {
  if (!r.ok) {
    m.error = r.error;
    return;
  }
  auto add = [&m](std::string key, std::string value) {
    m.details.emplace_back(std::move(key), std::move(value));
  };
  m.headline = length(r.minimum, o);
  add("Distance", m.headline);
  add("\xCE\x94X", length(r.delta.X(), o)); // ΔX
  add("\xCE\x94Y", length(r.delta.Y(), o));
  add("\xCE\x94Z", length(r.delta.Z(), o));
  if (r.parallelDistance) {
    add("Parallel distance", length(*r.parallelDistance, o));
  }
  if (r.centerDistance) {
    add("Center to center", length(*r.centerDistance, o));
  }
  add("Point 1", point(r.point1, o));
  add("Point 2", point(r.point2, o));
  if (r.tolerance > 0.0) {
    add("Tolerance", tolerance(r.tolerance, o));
  }

  Witness& w = m.witness;
  w.polylines.push_back({r.point1, r.point2});
  w.markers = {r.point1, r.point2};
  if (r.center1 && r.center2 && r.center1->Distance(*r.center2) > Precision::Confusion()) {
    w.polylines.push_back({*r.center1, *r.center2});
    w.markers.push_back(*r.center1);
    w.markers.push_back(*r.center2);
  }
  w.labelAnchor = midpoint(r.point1, r.point2);
  w.label = m.headline;
  m.ok = true;
}

void angleReport(const std::vector<Pick>& picks, const MeasureDisplayOptions& o, Measurement& m) {
  const measure::AngleResult r = measure::angle(picks[0].shape, picks[1].shape);
  if (!r.ok) {
    m.error = r.error;
    return;
  }
  m.headline = angleText(r.angle, o);
  m.details.emplace_back("Angle", m.headline);
  m.details.emplace_back("Supplement", angleText(r.supplement, o));

  Witness& w = m.witness;
  w.label = m.headline;
  if (r.apex) {
    // Rays along both directions, as long as the nearer pick, with an arc between them.
    const double reach =
        std::max({std::min(r.apex->Distance(picks[0].point), r.apex->Distance(picks[1].point)),
                  Precision::Confusion()});
    const gp_Pnt end1 = r.apex->Translated(gp_Vec(r.direction1) * reach);
    const gp_Pnt end2 = r.apex->Translated(gp_Vec(r.direction2) * reach);
    w.polylines.push_back({end1, *r.apex, end2});
    w.markers.push_back(*r.apex);

    const double arcRadius = 0.6 * reach;
    const gp_Vec u(r.direction1);
    gp_Vec v = gp_Vec(r.direction2) - u * u.Dot(gp_Vec(r.direction2));
    if (v.Magnitude() > Precision::Angular()) {
      v.Normalize();
      std::vector<gp_Pnt> arc;
      const int segments = 32;
      for (int i = 0; i <= segments; ++i) {
        const double t = r.angle * i / segments;
        arc.push_back(r.apex->Translated(u * (arcRadius * std::cos(t)) + v * (arcRadius * std::sin(t))));
      }
      w.labelAnchor = arc[segments / 2];
      w.polylines.push_back(std::move(arc));
    } else {
      w.labelAnchor = end1;
    }
  } else {
    w.polylines.push_back({picks[0].point, picks[1].point});
    w.markers = {picks[0].point, picks[1].point};
    w.labelAnchor = midpoint(picks[0].point, picks[1].point);
  }
  m.ok = true;
}

void radiusReport(const Pick& pick, const MeasureDisplayOptions& o, Measurement& m) {
  const auto r = measure::radius(pick.shape);
  if (!r) {
    m.error = "Pick a circular edge or a cylindrical, spherical or toroidal face.";
    return;
  }
  const std::string radiusText = length(r->radius, o);
  const std::string diameterText = length(r->diameter, o);
  m.headline = "R " + radiusText + "  \xE2\x8C\x80 " + diameterText; // ⌀
  m.details.emplace_back("Radius", radiusText);
  m.details.emplace_back("Diameter", diameterText);
  m.details.emplace_back("Center", point(r->center, o));
  if (!r->isSphere) {
    m.details.emplace_back("Axis", direction(r->axis.Direction()));
  }

  Witness& w = m.witness;
  const gp_Dir radial = r->isSphere
                            ? gp_Dir(gp_Vec(r->center, pick.point).Magnitude() > Precision::Confusion()
                                         ? gp_Vec(r->center, pick.point)
                                         : gp_Vec(1, 0, 0))
                            : radialDirection(gp_Ax1(r->center, r->axis.Direction()), pick.point);
  const gp_Pnt onCircle = r->center.Translated(gp_Vec(radial) * r->radius);
  w.polylines.push_back({r->center, onCircle});
  if (!r->isSphere) {
    w.polylines.push_back(circle(r->center, r->axis.Direction(), radial, r->radius));
  }
  w.markers = {r->center, onCircle};
  w.labelAnchor = midpoint(r->center, onCircle);
  w.label = "R " + radiusText;
  m.ok = true;
}

std::string csvField(const std::string& text) {
  if (text.find_first_of(",\"\n") == std::string::npos) {
    return text;
  }
  std::string quoted = "\"";
  for (const char c : text) {
    if (c == '"') {
      quoted += '"';
    }
    quoted += c;
  }
  return quoted + "\"";
}

} // namespace

const char* toString(MeasureMode mode) {
  switch (mode) {
  case MeasureMode::EntityInfo:
    return "Entity info";
  case MeasureMode::Distance:
    return "Distance";
  case MeasureMode::PointToPoint:
    return "Point to point";
  case MeasureMode::Angle:
    return "Angle";
  case MeasureMode::Radius:
    return "Radius";
  }
  return "Measurement";
}

int picksRequired(MeasureMode mode) {
  switch (mode) {
  case MeasureMode::EntityInfo:
  case MeasureMode::Radius:
    return 1;
  case MeasureMode::Distance:
  case MeasureMode::PointToPoint:
  case MeasureMode::Angle:
    return 2;
  }
  return 1;
}

const char* toString(SnapKind kind) {
  switch (kind) {
  case SnapKind::Vertex:
    return "Vertex";
  case SnapKind::Endpoint:
    return "Endpoint";
  case SnapKind::Midpoint:
    return "Midpoint";
  case SnapKind::Center:
    return "Center";
  case SnapKind::OnEdge:
    return "On edge";
  case SnapKind::OnFace:
    return "On face";
  case SnapKind::Free:
    break;
  }
  return "Point";
}

SnapResult snap(const TopoDS_Shape& shape, const gp_Pnt& picked, double snapDistance, bool projectOnFace) {
  if (shape.IsNull()) {
    return {picked, SnapKind::Free};
  }
  switch (shape.ShapeType()) {
  case TopAbs_VERTEX:
    return {BRep_Tool::Pnt(TopoDS::Vertex(shape)), SnapKind::Vertex};
  case TopAbs_EDGE: {
    const EntityInfo info = measure::entityInfo(shape);
    const measure::EdgeInfo& e = *info.edge;
    if (e.curve == CurveKind::Circle && e.axis) {
      return {e.axis->Location(), SnapKind::Center};
    }
    std::vector<SnapResult> candidates{{e.midpoint, SnapKind::Midpoint}};
    if (!e.closed) {
      candidates.push_back({e.start, SnapKind::Endpoint});
      candidates.push_back({e.end, SnapKind::Endpoint});
    }
    const auto nearest = std::min_element(candidates.begin(), candidates.end(),
                                          [&picked](const SnapResult& a, const SnapResult& b) {
                                            return a.point.Distance(picked) < b.point.Distance(picked);
                                          });
    if (nearest->point.Distance(picked) <= snapDistance) {
      return *nearest;
    }
    return {projectOnto(shape, picked), SnapKind::OnEdge};
  }
  case TopAbs_FACE:
    return {projectOnFace ? projectOnto(shape, picked) : picked, SnapKind::OnFace};
  default:
    return {picked, SnapKind::Free};
  }
}

gp_Pnt snapPoint(const TopoDS_Shape& shape, const gp_Pnt& picked, double snapDistance) {
  return snap(shape, picked, snapDistance).point;
}

std::string describeEntity(const TopoDS_Shape& shape) {
  const EntityInfo info = measure::entityInfo(shape);
  std::string text = measure::toString(info.kind);
  if (info.edge) {
    text += std::string(" (") + measure::toString(info.edge->curve) + ")";
  } else if (info.face) {
    text += std::string(" (") + measure::toString(info.face->surface) + ")";
  }
  return text;
}

Measurement computeMeasurement(MeasureMode mode, const std::vector<Pick>& picks,
                               const MeasureDisplayOptions& options, double snapDistance) {
  Measurement m;
  m.mode = mode;
  if (static_cast<int>(picks.size()) != picksRequired(mode)) {
    m.error = "Wrong number of picks.";
    return m;
  }
  for (const Pick& pick : picks) {
    std::string entity = describeEntity(pick.shape);
    if (!pick.owner.empty()) {
      entity += " on " + pick.owner;
    }
    m.entities.push_back(std::move(entity));
  }

  switch (mode) {
  case MeasureMode::EntityInfo:
    entityInfoReport(picks[0], options, m);
    break;
  case MeasureMode::Distance:
    distanceReport(measure::distance(picks[0].shape, picks[1].shape), options, m);
    break;
  case MeasureMode::PointToPoint: {
    const gp_Pnt a = snapPoint(picks[0].shape, picks[0].point, snapDistance);
    const gp_Pnt b = snapPoint(picks[1].shape, picks[1].point, snapDistance);
    distanceReport(measure::distance(a, b), options, m);
    break;
  }
  case MeasureMode::Angle:
    angleReport(picks, options, m);
    break;
  case MeasureMode::Radius:
    radiusReport(picks[0], options, m);
    break;
  }
  return m;
}

std::string toClipboardText(const Measurement& m) {
  std::string text = std::string(toString(m.mode)) + ": " + (m.ok ? m.headline : m.error) + "\n";
  for (const std::string& entity : m.entities) {
    text += "  " + entity + "\n";
  }
  for (const auto& [key, value] : m.details) {
    text += "  " + key + ": " + value + "\n";
  }
  return text;
}

std::string toCsv(const std::vector<Measurement>& measurements) {
  std::string csv = "Mode,Value,Entities,Details\n";
  for (const Measurement& m : measurements) {
    std::string entities;
    for (const std::string& entity : m.entities) {
      entities += (entities.empty() ? "" : "; ") + entity;
    }
    std::string details;
    for (const auto& [key, value] : m.details) {
      details += (details.empty() ? "" : "; ") + key + ": " + value;
    }
    csv += csvField(toString(m.mode)) + "," + csvField(m.ok ? m.headline : m.error) + "," +
           csvField(entities) + "," + csvField(details) + "\n";
  }
  return csv;
}

} // namespace sc::core
