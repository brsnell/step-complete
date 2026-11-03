#pragma once

#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Mat.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <array>
#include <optional>
#include <string>
#include <vector>

// Measurement on the exact B-rep (never the display mesh).
//
// All functions are stateless and take shapes already placed in world
// coordinates: a sub-shape picked on an instance is in its prototype's frame,
// so pass toWorld(subShape, instanceLocation). All lengths are in millimeters,
// angles in radians. Nothing here depends on Qt or the viewer.

namespace sc::core::measure {

// Places a prototype-space sub-shape at an instance's world location.
TopoDS_Shape toWorld(const TopoDS_Shape& shape, const TopLoc_Location& instanceLocation);

enum class EntityKind { Vertex, Edge, Face, Solid, Shell, Compound, Other };

enum class CurveKind { Line, Circle, Ellipse, Hyperbola, Parabola, Bezier, BSpline, Offset, Other };

enum class SurfaceKind {
  Plane,
  Cylinder,
  Cone,
  Sphere,
  Torus,
  Bezier,
  BSpline,
  Revolution,
  Extrusion,
  Offset,
  Other
};

const char* toString(EntityKind kind);
const char* toString(CurveKind kind);
const char* toString(SurfaceKind kind);

// --- Entity info -----------------------------------------------------------------

struct EdgeInfo {
  CurveKind curve = CurveKind::Other;
  double length = 0.0;
  gp_Pnt start, end, midpoint; // midpoint by arc length
  bool closed = false;
  std::optional<gp_Dir> direction; // lines: start -> end
  std::optional<double> radius;    // circles; ellipses: major radius
  std::optional<double> minorRadius;
  std::optional<gp_Ax1> axis;       // circles/ellipses: center and normal
  std::optional<double> sweepAngle; // circular arcs
};

struct FaceInfo {
  SurfaceKind surface = SurfaceKind::Other;
  double area = 0.0;
  gp_Pnt centroid;
  std::optional<gp_Dir> normal;      // planes: outward with respect to face orientation
  std::optional<gp_Ax1> axis;        // cylinder, cone, sphere (through center), torus, revolution
  std::optional<double> radius;      // cylinder, sphere, torus major radius; cone reference radius
  std::optional<double> minorRadius; // torus
  std::optional<double> halfAngle;   // cone semi-angle
};

struct SolidInfo {
  double volume = 0.0;
  double area = 0.0;
  gp_Pnt centroid;
};

struct EntityInfo {
  EntityKind kind = EntityKind::Other;
  gp_Pnt point; // vertex position
  std::optional<EdgeInfo> edge;
  std::optional<FaceInfo> face;
  std::optional<SolidInfo> solid;
  // Largest B-rep tolerance among the entity and its sub-shapes: results
  // smaller than this are at the model's noise floor.
  double tolerance = 0.0;
};

EntityInfo entityInfo(const TopoDS_Shape& shape);

// --- Distance --------------------------------------------------------------------

struct DistanceResult {
  bool ok = false;
  std::string error;
  double minimum = 0.0;
  gp_Pnt point1, point2; // closest points (witness)
  gp_Vec delta;          // point2 - point1
  // Between the "centers" of the two entities when both have one: circle and
  // sphere centers are points, cylinder/cone/torus axes are lines. Point-point,
  // point-line and parallel line-line combinations are reported.
  std::optional<double> centerDistance;
  std::optional<gp_Pnt> center1, center2;
  // Parallel planar faces or parallel straight edges: their separation.
  std::optional<double> parallelDistance;
  double tolerance = 0.0;
};

DistanceResult distance(const TopoDS_Shape& a, const TopoDS_Shape& b);
DistanceResult distance(const gp_Pnt& a, const gp_Pnt& b);

// --- Angle -----------------------------------------------------------------------

struct AngleResult {
  bool ok = false;
  std::string error;
  double angle = 0.0;      // radians
  double supplement = 0.0; // pi - angle
  gp_Dir direction1, direction2;
  // Set when the two entities meet (straight edges sharing a vertex, or
  // intersecting lines/axes): the apex for drawing the angle arc.
  std::optional<gp_Pnt> apex;
};

// Straight edges, planar faces (via normal) and faces or edges with an axis
// (cylinder, cone, circle). Two straight edges sharing a vertex give the
// interior angle in [0, pi]; all other combinations give the undirected angle
// in [0, pi/2].
AngleResult angle(const TopoDS_Shape& a, const TopoDS_Shape& b);

// --- Radius ----------------------------------------------------------------------

struct RadiusResult {
  double radius = 0.0;
  double diameter = 0.0;
  gp_Ax1 axis; // center and normal (circles) or axis (cylinder/sphere/torus)
  gp_Pnt center;
  bool isSphere = false;
};

// Circular edges and cylindrical, spherical and toroidal (major radius) faces.
std::optional<RadiusResult> radius(const TopoDS_Shape& shape);

// --- Mass properties -------------------------------------------------------------

struct OrientedBox {
  gp_Pnt center;
  std::array<gp_Dir, 3> axes;
  std::array<double, 3> halfSizes{};
};

struct MassProperties {
  bool ok = false;
  bool hasVolume = false; // false when the selection contains no solids
  int solidCount = 0;
  double volume = 0.0;
  double area = 0.0;
  gp_Pnt centroid; // of the volume, or of the surface when there is no volume
  // Inertia tensor about the centroid, for unit density (mm^5).
  gp_Mat inertia;
  std::array<double, 3> principalMoments{};
  std::array<gp_Dir, 3> principalAxes;
  gp_Pnt boxMin, boxMax; // axis-aligned, exact geometry
  OrientedBox orientedBox;
};

MassProperties massProperties(const std::vector<TopoDS_Shape>& shapes);

} // namespace sc::core::measure
