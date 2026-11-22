#include "core/Measurement.h"

#include <TopExp_Explorer.hxx>

#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Tool.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <set>

namespace sc::core {

namespace {

using measure::CurveKind;
using measure::EntityInfo;
using measure::SurfaceKind;

constexpr double Pi = 3.14159265358979323846;

std::string point(const gp_Pnt& location, const MeasureDisplayOptions& options) {
  return formatNumber(fromMillimeters(location.X(), options.lengthUnit), options.decimals) + ", " +
         formatNumber(fromMillimeters(location.Y(), options.lengthUnit), options.decimals) + ", " +
         formatNumber(fromMillimeters(location.Z(), options.lengthUnit), options.decimals) + " " +
         std::string(symbol(options.lengthUnit));
}

std::string direction(const gp_Dir& dir) {
  return formatNumber(dir.X(), 4) + ", " + formatNumber(dir.Y(), 4) + ", " + formatNumber(dir.Z(), 4);
}

std::string length(double mm, const MeasureDisplayOptions& options) {
  return formatLength(mm, options.lengthUnit, options.decimals);
}

// Model tolerances are often far below the display precision; say so instead
// of printing a misleading "0.000".
std::string tolerance(double mm, const MeasureDisplayOptions& options) {
  const double shown = fromMillimeters(mm, options.lengthUnit);
  const double resolution = std::pow(10.0, -std::clamp(options.decimals, 0, 12));
  if (shown < resolution / 2.0) {
    return "< " +
           formatLength(toMillimeters(resolution, options.lengthUnit), options.lengthUnit, options.decimals);
  }
  return length(mm, options);
}

std::string angleText(double radians, const MeasureDisplayOptions& options) {
  return formatAngle(radians, options.angleUnit, options.angleDecimals);
}

gp_Pnt midpoint(const gp_Pnt& a, const gp_Pnt& b) {
  return gp_Pnt((a.XYZ() + b.XYZ()) / 2.0);
}

// Closest point on a shape to a point (exact B-rep).
gp_Pnt projectOnto(const TopoDS_Shape& shape, const gp_Pnt& point) {
  try {
    BRepExtrema_DistShapeShape extrema(BRepBuilderAPI_MakeVertex(point).Vertex(), shape, Extrema_ExtFlag_MIN);
    if (extrema.IsDone() && extrema.NbSolution() > 0) {
      return extrema.PointOnShape2(1);
    }
  } catch (const Standard_Failure&) {
  }
  return point;
}

// Any unit vector perpendicular to 'axis', preferring the direction towards 'toward'.
gp_Dir radialDirection(const gp_Ax1& axis, const gp_Pnt& toward) {
  const gp_Vec axisDir(axis.Direction());
  gp_Vec v(axis.Location(), toward);
  v -= axisDir * v.Dot(axisDir);
  if (v.Magnitude() > Precision::Confusion()) {
    return gp_Dir(v);
  }
  const gp_Vec helper = std::abs(axisDir.X()) < 0.9 ? gp_Vec(1, 0, 0) : gp_Vec(0, 1, 0);
  return gp_Dir(axisDir.Crossed(helper));
}

std::vector<gp_Pnt> circle(const gp_Pnt& center, const gp_Dir& normal, const gp_Dir& start, double radius,
                           int segments = 64) {
  const gp_Vec u(start);
  const gp_Vec across = gp_Vec(normal).Crossed(u);
  std::vector<gp_Pnt> points;
  for (int i = 0; i <= segments; ++i) {
    const double angle = 2.0 * Pi * i / segments;
    points.push_back(center.Translated(u * (radius * std::cos(angle)) + across * (radius * std::sin(angle))));
  }
  return points;
}

void entityInfoReport(const Pick& pick, const MeasureDisplayOptions& options, Measurement& measurement) {
  const EntityInfo info = measure::entityInfo(pick.shape);
  auto add = [&measurement](std::string key, std::string value) {
    measurement.details.emplace_back(std::move(key), std::move(value));
  };
  Witness& witness = measurement.witness;
  witness.labelAnchor = pick.point;

  switch (info.kind) {
  case measure::EntityKind::Vertex:
    measurement.headline = point(info.point, options);
    add("Position", measurement.headline);
    witness.markers.push_back(info.point);
    witness.labelAnchor = info.point;
    break;
  case measure::EntityKind::Edge: {
    const measure::EdgeInfo& edge = *info.edge;
    measurement.headline = length(edge.length, options);
    add("Type", measure::toString(edge.curve));
    add("Length", measurement.headline);
    if (edge.radius) {
      add(edge.curve == CurveKind::Ellipse ? "Major radius" : "Radius", length(*edge.radius, options));
      if (edge.curve == CurveKind::Circle) {
        add("Diameter", length(2.0 * *edge.radius, options));
      }
    }
    if (edge.minorRadius) {
      add("Minor radius", length(*edge.minorRadius, options));
    }
    if (edge.axis) {
      add("Center", point(edge.axis->Location(), options));
      add("Axis", direction(edge.axis->Direction()));
    }
    if (edge.sweepAngle && !edge.closed) {
      add("Arc angle", angleText(*edge.sweepAngle, options));
    }
    if (edge.direction) {
      add("Direction", direction(*edge.direction));
    }
    if (!edge.closed) {
      add("Start", point(edge.start, options));
      add("End", point(edge.end, options));
      witness.markers.push_back(edge.start);
      witness.markers.push_back(edge.end);
    }
    add("Midpoint", point(edge.midpoint, options));
    witness.labelAnchor = edge.midpoint;
    break;
  }
  case measure::EntityKind::Face: {
    const measure::FaceInfo& face = *info.face;
    measurement.headline = formatArea(face.area, options.lengthUnit, options.decimals);
    add("Type", measure::toString(face.surface));
    add("Area", measurement.headline);
    if (face.normal) {
      add("Normal", direction(*face.normal));
    }
    if (face.radius) {
      add(face.surface == SurfaceKind::Torus  ? "Major radius"
          : face.surface == SurfaceKind::Cone ? "Reference radius"
                                              : "Radius",
          length(*face.radius, options));
      if (face.surface == SurfaceKind::Cylinder || face.surface == SurfaceKind::Sphere) {
        add("Diameter", length(2.0 * *face.radius, options));
      }
    }
    if (face.minorRadius) {
      add("Minor radius", length(*face.minorRadius, options));
    }
    if (face.halfAngle) {
      add("Half angle", angleText(*face.halfAngle, options));
    }
    if (face.axis) {
      add(face.surface == SurfaceKind::Sphere ? "Center" : "Axis point",
          point(face.axis->Location(), options));
      if (face.surface != SurfaceKind::Sphere) {
        add("Axis", direction(face.axis->Direction()));
      }
    }
    add("Centroid", point(face.centroid, options));
    witness.markers.push_back(pick.point);
    break;
  }
  default:
    measurement.error = "Pick a vertex, edge or face.";
    return;
  }
  add("Tolerance", tolerance(info.tolerance, options));
  witness.label = measurement.headline;
  measurement.ok = true;
}

void distanceReport(const measure::DistanceResult& result, const MeasureDisplayOptions& options,
                    Measurement& measurement) {
  if (!result.ok) {
    measurement.error = result.error;
    return;
  }
  auto add = [&measurement](std::string key, std::string value) {
    measurement.details.emplace_back(std::move(key), std::move(value));
  };
  measurement.headline = length(result.minimum, options);
  add("Distance", measurement.headline);
  add("\xCE\x94X", length(result.delta.X(), options)); // ΔX
  add("\xCE\x94Y", length(result.delta.Y(), options));
  add("\xCE\x94Z", length(result.delta.Z(), options));
  if (result.parallelDistance) {
    add("Parallel distance", length(*result.parallelDistance, options));
  }
  if (result.centerDistance) {
    add("Center to center", length(*result.centerDistance, options));
  }
  add("Point 1", point(result.point1, options));
  add("Point 2", point(result.point2, options));
  if (result.tolerance > 0.0) {
    add("Tolerance", tolerance(result.tolerance, options));
  }

  Witness& witness = measurement.witness;
  witness.polylines.push_back({result.point1, result.point2});
  witness.markers = {result.point1, result.point2};
  if (result.center1 && result.center2 &&
      result.center1->Distance(*result.center2) > Precision::Confusion()) {
    witness.polylines.push_back({*result.center1, *result.center2});
    witness.markers.push_back(*result.center1);
    witness.markers.push_back(*result.center2);
  }
  witness.labelAnchor = midpoint(result.point1, result.point2);
  witness.label = measurement.headline;
  measurement.ok = true;
}

void angleReport(const std::vector<Pick>& picks, const MeasureDisplayOptions& options,
                 Measurement& measurement) {
  const measure::AngleResult result = measure::angle(picks[0].shape, picks[1].shape);
  if (!result.ok) {
    measurement.error = result.error;
    return;
  }
  measurement.headline = angleText(result.angle, options);
  measurement.details.emplace_back("Angle", measurement.headline);
  measurement.details.emplace_back("Supplement", angleText(result.supplement, options));

  Witness& witness = measurement.witness;
  witness.label = measurement.headline;
  if (result.apex) {
    // Rays along both directions, as long as the nearer pick, with an arc between them.
    const double reach =
        std::max({std::min(result.apex->Distance(picks[0].point), result.apex->Distance(picks[1].point)),
                  Precision::Confusion()});
    const gp_Pnt end1 = result.apex->Translated(gp_Vec(result.direction1) * reach);
    const gp_Pnt end2 = result.apex->Translated(gp_Vec(result.direction2) * reach);
    witness.polylines.push_back({end1, *result.apex, end2});
    witness.markers.push_back(*result.apex);

    const double arcRadius = 0.6 * reach;
    const gp_Vec u(result.direction1);
    gp_Vec v = gp_Vec(result.direction2) - u * u.Dot(gp_Vec(result.direction2));
    if (v.Magnitude() > Precision::Angular()) {
      v.Normalize();
      std::vector<gp_Pnt> arc;
      const int segments = 32;
      for (int i = 0; i <= segments; ++i) {
        const double stepAngle = result.angle * i / segments;
        arc.push_back(result.apex->Translated(u * (arcRadius * std::cos(stepAngle)) +
                                              v * (arcRadius * std::sin(stepAngle))));
      }
      witness.labelAnchor = arc[segments / 2];
      witness.polylines.push_back(std::move(arc));
    } else {
      witness.labelAnchor = end1;
    }
  } else {
    witness.polylines.push_back({picks[0].point, picks[1].point});
    witness.markers = {picks[0].point, picks[1].point};
    witness.labelAnchor = midpoint(picks[0].point, picks[1].point);
  }
  measurement.ok = true;
}

void radiusReport(const Pick& pick, const MeasureDisplayOptions& options, Measurement& measurement) {
  const auto result = measure::radius(pick.shape);
  if (!result) {
    measurement.error = "Pick a circular edge or a cylindrical, spherical or toroidal face.";
    return;
  }
  const std::string radiusText = length(result->radius, options);
  const std::string diameterText = length(result->diameter, options);
  measurement.headline = "R " + radiusText + "  \xE2\x8C\x80 " + diameterText; // ⌀
  measurement.details.emplace_back("Radius", radiusText);
  measurement.details.emplace_back("Diameter", diameterText);
  measurement.details.emplace_back("Center", point(result->center, options));
  if (!result->isSphere) {
    measurement.details.emplace_back("Axis", direction(result->axis.Direction()));
  }

  Witness& witness = measurement.witness;
  const gp_Dir radial = result->isSphere
                            ? gp_Dir(gp_Vec(result->center, pick.point).Magnitude() > Precision::Confusion()
                                         ? gp_Vec(result->center, pick.point)
                                         : gp_Vec(1, 0, 0))
                            : radialDirection(gp_Ax1(result->center, result->axis.Direction()), pick.point);
  const gp_Pnt onCircle = result->center.Translated(gp_Vec(radial) * result->radius);
  witness.polylines.push_back({result->center, onCircle});
  if (!result->isSphere) {
    witness.polylines.push_back(circle(result->center, result->axis.Direction(), radial, result->radius));
  }
  witness.markers = {result->center, onCircle};
  witness.labelAnchor = midpoint(result->center, onCircle);
  witness.label = "R " + radiusText;
  measurement.ok = true;
}

// --- Mass properties -------------------------------------------------------------

// A model "matches" its declared validation properties when the volume and area agree to
// 0.1% (STEP writers state them to a handful of digits, so real matches are far tighter) and
// the centroid is within 0.1% of the bounding-box diagonal, but never tighter than 1e-6 mm so
// that tiny parts do not demand sub-nanometer agreement.
constexpr double RelativeVolumeTolerance = 1e-3;
constexpr double RelativeAreaTolerance = 1e-3;
constexpr double CentroidDiagonalFraction = 1e-3;
constexpr double CentroidFloor = 1e-6; // mm
constexpr size_t MaxListedEntities = 10;

// Moments of inertia are length^5 for unit density (mass = volume). They span many
// orders of magnitude across units (mm^5 vs in^5), so use significant digits.
std::string moment(double mm5, const MeasureDisplayOptions& options) {
  const double perUnit = millimetersPer(options.lengthUnit);
  return formatSignificant(mm5 / std::pow(perUnit, 5), 6) + " " + std::string(symbol(options.lengthUnit)) +
         "\xE2\x81\xB5"; // superscript five
}

// Mass moments of inertia (kg mm² from the measure code) in the display units, e.g. "kg·mm²".
std::string massMoment(double kgMm2, const MeasureDisplayOptions& options) {
  const double perUnit = millimetersPer(options.lengthUnit);
  return formatSignificant(kgMm2 / kilogramsPer(options.massUnit) / (perUnit * perUnit), 6) + " " +
         std::string(symbol(options.massUnit)) + "\xC2\xB7" + std::string(symbol(options.lengthUnit)) +
         "\xC2\xB2"; // middle dot, superscript two
}

bool hasSolid(const TopoDS_Shape& shape) {
  return !shape.IsNull() && TopExp_Explorer(shape, TopAbs_SOLID).More();
}

// "10.000 x 20.000 x 30.000 mm" (with multiplication signs)
std::string sizeText(double sizeX, double sizeY, double sizeZ, const MeasureDisplayOptions& options) {
  auto format = [&options](double mm) {
    return formatNumber(fromMillimeters(mm, options.lengthUnit), options.decimals);
  };
  return format(sizeX) + " \xC3\x97 " + format(sizeY) + " \xC3\x97 " + format(sizeZ) + " " +
         std::string(symbol(options.lengthUnit));
}

// Signed relative difference of 'computed' against 'declared', as a percentage like "+0.42%".
std::string percentText(double computed, double declared) {
  const double percent = declared != 0.0 ? 100.0 * (computed - declared) / std::abs(declared) : 0.0;
  const std::string number = formatNumber(percent, 2);
  return (number.front() == '-' ? "" : "+") + number + "%";
}

bool relativelyDiffers(double computed, double declared, double tolerance) {
  if (declared == 0.0) {
    return computed != 0.0;
  }
  return std::abs(computed - declared) / std::abs(declared) > tolerance;
}

void validationRows(const MassItem& item, const measure::MassProperties& mass,
                    const MeasureDisplayOptions& options, Measurement& measurement) {
  const ValidationProperties& declared = *item.declared;
  auto add = [&measurement](std::string key, std::string value) {
    measurement.details.emplace_back(std::move(key), std::move(value));
  };
  std::vector<std::string> differences;

  if (declared.volume) {
    add("Declared volume", formatVolume(*declared.volume, options.lengthUnit, options.decimals));
    if (mass.hasVolume && relativelyDiffers(mass.volume, *declared.volume, RelativeVolumeTolerance)) {
      differences.push_back("volume " + percentText(mass.volume, *declared.volume));
    }
  }
  if (declared.area) {
    add("Declared area", formatArea(*declared.area, options.lengthUnit, options.decimals));
    if (relativelyDiffers(mass.area, *declared.area, RelativeAreaTolerance)) {
      differences.push_back("area " + percentText(mass.area, *declared.area));
    }
  }
  if (declared.centroid) {
    // Declared centroids are in the part's own coordinates; place them like the geometry.
    const gp_Pnt world = declared.centroid->Transformed(item.location.Transformation());
    add("Declared centroid", point(world, options));
    const double diagonal = mass.boxMin.Distance(mass.boxMax);
    const double allowed = std::max(CentroidDiagonalFraction * diagonal, CentroidFloor);
    const double offset = world.Distance(mass.centroid);
    if (offset > allowed) {
      differences.push_back("centroid " + length(offset, options) + " off");
    }
  }

  std::string verdict = "Matches";
  if (!differences.empty()) {
    verdict = "Differs: ";
    for (size_t i = 0; i < differences.size(); ++i) {
      verdict += (i == 0 ? "" : ", ") + differences[i];
    }
  }
  add("Validation", verdict);
}

// The 12 edges of the axis-aligned bounding box.
void boxWitness(const measure::MassProperties& mass, Witness& witness) {
  const gp_Pnt& boxMin = mass.boxMin;
  const gp_Pnt& boxMax = mass.boxMax;
  auto corner = [&](int i) {
    return gp_Pnt((i & 1) ? boxMax.X() : boxMin.X(), (i & 2) ? boxMax.Y() : boxMin.Y(),
                  (i & 4) ? boxMax.Z() : boxMin.Z());
  };
  // Corner indices differ in exactly one bit along an edge.
  for (int i = 0; i < 8; ++i) {
    for (int bit = 1; bit <= 4; bit <<= 1) {
      if ((i & bit) == 0) {
        witness.polylines.push_back({corner(i), corner(i | bit)});
      }
    }
  }
}

std::string csvField(const std::string& text) {
  if (text.find_first_of(",\"\n") == std::string::npos) {
    return text;
  }
  std::string quoted = "\"";
  for (const char character : text) {
    if (character == '"') {
      quoted += '"';
    }
    quoted += character;
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
  case MeasureMode::MassProperties:
    return "Mass properties";
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
  case MeasureMode::MassProperties:
    return 0;
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
    const measure::EdgeInfo& edge = *info.edge;
    if (edge.curve == CurveKind::Circle && edge.axis) {
      return {edge.axis->Location(), SnapKind::Center};
    }
    std::vector<SnapResult> candidates{{edge.midpoint, SnapKind::Midpoint}};
    if (!edge.closed) {
      candidates.push_back({edge.start, SnapKind::Endpoint});
      candidates.push_back({edge.end, SnapKind::Endpoint});
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
  Measurement measurement;
  measurement.mode = mode;
  if (static_cast<int>(picks.size()) != picksRequired(mode)) {
    measurement.error = "Wrong number of picks.";
    return measurement;
  }
  for (const Pick& pick : picks) {
    std::string entity = describeEntity(pick.shape);
    if (!pick.owner.empty()) {
      entity += " on " + pick.owner;
    }
    measurement.entities.push_back(std::move(entity));
  }

  switch (mode) {
  case MeasureMode::EntityInfo:
    entityInfoReport(picks[0], options, measurement);
    break;
  case MeasureMode::Distance:
    distanceReport(measure::distance(picks[0].shape, picks[1].shape), options, measurement);
    break;
  case MeasureMode::PointToPoint: {
    const gp_Pnt a = snapPoint(picks[0].shape, picks[0].point, snapDistance);
    const gp_Pnt b = snapPoint(picks[1].shape, picks[1].point, snapDistance);
    distanceReport(measure::distance(a, b), options, measurement);
    break;
  }
  case MeasureMode::Angle:
    angleReport(picks, options, measurement);
    break;
  case MeasureMode::Radius:
    radiusReport(picks[0], options, measurement);
    break;
  case MeasureMode::MassProperties:
    measurement.error = "Mass properties works on the selected parts, not on picks.";
    break;
  }
  return measurement;
}

Measurement computeMassProperties(const std::vector<MassItem>& items, const MeasureDisplayOptions& options) {
  Measurement measurement;
  measurement.mode = MeasureMode::MassProperties;

  std::vector<TopoDS_Shape> shapes;
  for (const MassItem& item : items) {
    if (!item.worldShape.IsNull()) {
      shapes.push_back(item.worldShape);
    }
    if (measurement.entities.size() < MaxListedEntities) {
      measurement.entities.push_back(item.name.empty() ? "(unnamed)" : item.name);
    }
  }
  if (items.size() > MaxListedEntities) {
    measurement.entities.push_back("and " + std::to_string(items.size() - MaxListedEntities) + " more");
  }

  // Densities only when every item with volume has one: a partial mass would look like the answer.
  std::vector<double> densities;
  int withVolume = 0;
  int withoutDensity = 0;
  std::set<double> distinctDensities;
  std::set<std::string> materials;
  for (const MassItem& item : items) {
    if (item.worldShape.IsNull()) {
      continue;
    }
    if (hasSolid(item.worldShape)) {
      ++withVolume;
      if (!item.density || *item.density <= 0.0) {
        ++withoutDensity;
      } else {
        distinctDensities.insert(*item.density);
        materials.insert(item.material);
      }
    }
    densities.push_back(item.density && *item.density > 0.0 ? *item.density : 1.0);
  }
  if (withVolume == 0 || withoutDensity > 0) {
    densities.clear();
  }
  const measure::MassProperties mass = measure::massProperties(shapes, densities);
  if (!mass.ok) {
    measurement.error = "There is no solid or surface geometry to measure.";
    return measurement;
  }

  auto add = [&measurement](std::string key, std::string value) {
    measurement.details.emplace_back(std::move(key), std::move(value));
  };
  const std::string volume = formatVolume(mass.volume, options.lengthUnit, options.decimals);
  const std::string area = formatArea(mass.area, options.lengthUnit, options.decimals);
  measurement.headline = mass.hasMass     ? formatMass(mass.mass, options.massUnit, options.decimals)
                         : mass.hasVolume ? volume
                                          : area;

  add("Solids", std::to_string(mass.solidCount));
  if (mass.hasMass) {
    add("Mass", measurement.headline);
    if (distinctDensities.size() == 1) {
      add("Density", formatNumber(*distinctDensities.begin(), 1) + " kg/m\xC2\xB3");
    }
    if (materials.size() == 1 && !materials.begin()->empty()) {
      add("Material", *materials.begin());
    }
  } else if (mass.hasVolume) {
    add("Mass", withoutDensity == withVolume ? std::string("no material set (Measure > Set Material)")
                                             : "no material for " + std::to_string(withoutDensity) + " of " +
                                                   std::to_string(withVolume) + " parts");
  }
  if (mass.hasVolume) {
    add("Volume", volume);
  }
  add("Surface area", area);
  add(mass.hasMass     ? "Center of mass"
      : mass.hasVolume ? "Centroid"
                       : "Centroid (surface)",
      point(mass.centroid, options));
  if (mass.hasMass) {
    add("Principal moments", massMoment(mass.principalMoments[0], options) + ", " +
                                 massMoment(mass.principalMoments[1], options) + ", " +
                                 massMoment(mass.principalMoments[2], options));
  } else if (mass.hasVolume) {
    // Inertia of a bare surface has other units and is rarely wanted: only report it for solids.
    add("Principal moments (unit density)", moment(mass.principalMoments[0], options) + ", " +
                                                moment(mass.principalMoments[1], options) + ", " +
                                                moment(mass.principalMoments[2], options));
  }
  add("Bounding box size", sizeText(mass.boxMax.X() - mass.boxMin.X(), mass.boxMax.Y() - mass.boxMin.Y(),
                                    mass.boxMax.Z() - mass.boxMin.Z(), options));
  add("Bounding box min", point(mass.boxMin, options));
  add("Bounding box max", point(mass.boxMax, options));
  std::array<double, 3> oriented = mass.orientedBox.halfSizes;
  if (oriented[0] > 0.0 || oriented[1] > 0.0 || oriented[2] > 0.0) {
    std::sort(oriented.begin(), oriented.end(), std::greater<double>());
    add("Oriented box size", sizeText(2.0 * oriented[0], 2.0 * oriented[1], 2.0 * oriented[2], options));
  }

  // Comparing with the file's claims only makes sense for one part instance: the declared
  // values of several parts cannot be combined reliably.
  if (items.size() == 1 && items.front().declared && !items.front().declared->empty()) {
    validationRows(items.front(), mass, options, measurement);
  }

  boxWitness(mass, measurement.witness);
  measurement.witness.markers.push_back(mass.centroid);
  measurement.witness.labelAnchor = mass.centroid;
  measurement.witness.label = measurement.headline;
  measurement.ok = true;
  return measurement;
}

std::string toClipboardText(const Measurement& measurement) {
  std::string text = std::string(toString(measurement.mode)) + ": " +
                     (measurement.ok ? measurement.headline : measurement.error) + "\n";
  for (const std::string& entity : measurement.entities) {
    text += "  " + entity + "\n";
  }
  for (const auto& [key, value] : measurement.details) {
    text += "  " + key + ": " + value + "\n";
  }
  return text;
}

std::string toCsv(const std::vector<Measurement>& measurements) {
  std::string csv = "Mode,Value,Entities,Details\n";
  for (const Measurement& measurement : measurements) {
    std::string entities;
    for (const std::string& entity : measurement.entities) {
      entities += (entities.empty() ? "" : "; ") + entity;
    }
    std::string details;
    for (const auto& [key, value] : measurement.details) {
      details += (details.empty() ? "" : "; ") + key + ": " + value;
    }
    csv += csvField(toString(measurement.mode)) + "," +
           csvField(measurement.ok ? measurement.headline : measurement.error) + "," + csvField(entities) +
           "," + csvField(details) + "\n";
  }
  return csv;
}

} // namespace sc::core
