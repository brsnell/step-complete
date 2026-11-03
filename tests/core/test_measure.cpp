// Measurement tests on analytic shapes with known answers.

#include "core/Measure.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace sc::core::measure;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTol = 1e-6;

double degrees(double radians) {
  return radians * 180.0 / kPi;
}

TopLoc_Location translation(double x, double y, double z) {
  gp_Trsf trsf;
  trsf.SetTranslation(gp_Vec(x, y, z));
  return TopLoc_Location(trsf);
}

// Rotation about an arbitrary axis followed by a translation: a "real" instance placement.
TopLoc_Location skewPlacement() {
  gp_Trsf rotation;
  rotation.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 2, 3)), 0.7);
  gp_Trsf move;
  move.SetTranslation(gp_Vec(120, -35, 48));
  return TopLoc_Location(move * rotation);
}

gp_Pnt transformed(const gp_Pnt& p, const TopLoc_Location& location) {
  return p.Transformed(location.Transformation());
}

// The sub-shape of the given type whose center of mass is closest to 'near'.
TopoDS_Shape closestSubShape(const TopoDS_Shape& shape, TopAbs_ShapeEnum type, const gp_Pnt& near) {
  TopoDS_Shape best;
  double bestDistance = std::numeric_limits<double>::max();
  for (TopExp_Explorer it(shape, type); it.More(); it.Next()) {
    GProp_GProps props;
    if (type == TopAbs_FACE) {
      BRepGProp::SurfaceProperties(it.Current(), props);
    } else {
      BRepGProp::LinearProperties(it.Current(), props);
    }
    const double d = props.CentreOfMass().Distance(near);
    if (d < bestDistance) {
      bestDistance = d;
      best = it.Current();
    }
  }
  return best;
}

TopoDS_Shape face(const TopoDS_Shape& shape, const gp_Pnt& near) {
  return closestSubShape(shape, TopAbs_FACE, near);
}

TopoDS_Shape faceOfType(const TopoDS_Shape& shape, SurfaceKind kind) {
  for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
    if (entityInfo(it.Current()).face->surface == kind) {
      return it.Current();
    }
  }
  return {};
}

TopoDS_Shape edgeOfType(const TopoDS_Shape& shape, CurveKind kind) {
  for (TopExp_Explorer it(shape, TopAbs_EDGE); it.More(); it.Next()) {
    const EntityInfo info = entityInfo(it.Current());
    if (info.edge && info.edge->curve == kind && info.edge->length > 0) {
      return it.Current();
    }
  }
  return {};
}

TopoDS_Shape segment(const gp_Pnt& a, const gp_Pnt& b) {
  return BRepBuilderAPI_MakeEdge(a, b).Edge();
}

TopoDS_Shape box(double dx, double dy, double dz) {
  return BRepPrimAPI_MakeBox(dx, dy, dz).Shape();
}

} // namespace

// --- Entity info -------------------------------------------------------------------

TEST(Measure, VertexPosition) {
  const auto vertex = BRepBuilderAPI_MakeVertex(gp_Pnt(1, 2, 3)).Shape();
  const EntityInfo info = entityInfo(vertex);
  EXPECT_EQ(info.kind, EntityKind::Vertex);
  EXPECT_TRUE(info.point.IsEqual(gp_Pnt(1, 2, 3), kTol));
}

TEST(Measure, StraightEdge) {
  const EntityInfo info = entityInfo(segment(gp_Pnt(0, 0, 0), gp_Pnt(3, 4, 0)));
  ASSERT_EQ(info.kind, EntityKind::Edge);
  ASSERT_TRUE(info.edge);
  EXPECT_EQ(info.edge->curve, CurveKind::Line);
  EXPECT_NEAR(info.edge->length, 5.0, kTol);
  EXPECT_TRUE(info.edge->midpoint.IsEqual(gp_Pnt(1.5, 2, 0), kTol));
  ASSERT_TRUE(info.edge->direction);
  EXPECT_TRUE(info.edge->direction->IsEqual(gp_Dir(3, 4, 0), kTol));
  EXPECT_FALSE(info.edge->closed);
  EXPECT_GT(info.tolerance, 0.0) << "B-rep tolerance must be reported";
}

TEST(Measure, CircularEdgeAndCylinderFace) {
  const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(5.0, 10.0).Shape();

  const EntityInfo circle = entityInfo(edgeOfType(cylinder, CurveKind::Circle));
  ASSERT_TRUE(circle.edge);
  EXPECT_NEAR(*circle.edge->radius, 5.0, kTol);
  EXPECT_NEAR(circle.edge->length, 2 * kPi * 5.0, 1e-6);
  EXPECT_NEAR(*circle.edge->sweepAngle, 2 * kPi, 1e-9);
  EXPECT_TRUE(circle.edge->closed);
  EXPECT_TRUE(circle.edge->axis->Direction().IsParallel(gp_Dir(0, 0, 1), 1e-9));

  const EntityInfo side = entityInfo(faceOfType(cylinder, SurfaceKind::Cylinder));
  ASSERT_TRUE(side.face);
  EXPECT_NEAR(side.face->area, 2 * kPi * 5.0 * 10.0, 1e-6);
  EXPECT_NEAR(*side.face->radius, 5.0, kTol);
  EXPECT_TRUE(side.face->axis->Direction().IsParallel(gp_Dir(0, 0, 1), 1e-9));
}

TEST(Measure, PlanarFaceNormalIsOutward) {
  const TopoDS_Shape solid = box(10, 20, 30);
  const EntityInfo top = entityInfo(face(solid, gp_Pnt(5, 10, 30)));
  ASSERT_TRUE(top.face);
  EXPECT_EQ(top.face->surface, SurfaceKind::Plane);
  EXPECT_NEAR(top.face->area, 200.0, kTol);
  EXPECT_TRUE(top.face->normal->IsEqual(gp_Dir(0, 0, 1), 1e-9));
  const EntityInfo bottom = entityInfo(face(solid, gp_Pnt(5, 10, 0)));
  EXPECT_TRUE(bottom.face->normal->IsEqual(gp_Dir(0, 0, -1), 1e-9));
  EXPECT_TRUE(top.face->centroid.IsEqual(gp_Pnt(5, 10, 30), kTol));
}

TEST(Measure, SphereConeAndTorusFaces) {
  const EntityInfo sphere = entityInfo(faceOfType(BRepPrimAPI_MakeSphere(4.0).Shape(), SurfaceKind::Sphere));
  ASSERT_TRUE(sphere.face);
  EXPECT_NEAR(sphere.face->area, 4 * kPi * 16.0, 1e-6);
  EXPECT_NEAR(*sphere.face->radius, 4.0, kTol);

  // Cone r1=10 -> r2=5 over h=5: semi-angle 45 degrees.
  const EntityInfo cone =
      entityInfo(faceOfType(BRepPrimAPI_MakeCone(10.0, 5.0, 5.0).Shape(), SurfaceKind::Cone));
  ASSERT_TRUE(cone.face);
  EXPECT_NEAR(degrees(*cone.face->halfAngle), 45.0, 1e-9);

  const EntityInfo torus =
      entityInfo(faceOfType(BRepPrimAPI_MakeTorus(10.0, 2.0).Shape(), SurfaceKind::Torus));
  ASSERT_TRUE(torus.face);
  EXPECT_NEAR(torus.face->area, 4 * kPi * kPi * 10.0 * 2.0, 1e-5);
  EXPECT_NEAR(*torus.face->radius, 10.0, kTol);
  EXPECT_NEAR(*torus.face->minorRadius, 2.0, kTol);
}

TEST(Measure, SolidInfo) {
  const EntityInfo info = entityInfo(box(10, 20, 30));
  EXPECT_EQ(info.kind, EntityKind::Solid);
  ASSERT_TRUE(info.solid);
  EXPECT_NEAR(info.solid->volume, 6000.0, 1e-6);
  EXPECT_NEAR(info.solid->area, 2200.0, 1e-6);
}

// --- Distance ----------------------------------------------------------------------

TEST(Measure, PointToPointDistance) {
  const DistanceResult d = distance(gp_Pnt(1, 1, 1), gp_Pnt(4, 5, 1));
  ASSERT_TRUE(d.ok);
  EXPECT_NEAR(d.minimum, 5.0, kTol);
  EXPECT_NEAR(d.delta.X(), 3.0, kTol);
  EXPECT_NEAR(d.delta.Y(), 4.0, kTol);
  EXPECT_NEAR(d.delta.Z(), 0.0, kTol);
}

TEST(Measure, MinimumDistanceBetweenSolidsWithWitnessPoints) {
  const TopoDS_Shape a = box(10, 10, 10);
  const TopoDS_Shape b = toWorld(box(10, 10, 10), translation(15, 3, 0));
  const DistanceResult d = distance(a, b);
  ASSERT_TRUE(d.ok) << d.error;
  EXPECT_NEAR(d.minimum, 5.0, kTol);
  EXPECT_NEAR(d.point1.X(), 10.0, kTol);
  EXPECT_NEAR(d.point2.X(), 15.0, kTol);
  EXPECT_NEAR(d.delta.Magnitude(), 5.0, kTol);
}

TEST(Measure, ParallelPlanarFaces) {
  const TopoDS_Shape a = box(10, 10, 10);
  const TopoDS_Shape b = toWorld(box(10, 10, 10), translation(17, 40, 0)); // faces don't overlap
  const DistanceResult d = distance(face(a, gp_Pnt(10, 5, 5)), face(b, gp_Pnt(17, 45, 5)));
  ASSERT_TRUE(d.ok) << d.error;
  ASSERT_TRUE(d.parallelDistance);
  EXPECT_NEAR(*d.parallelDistance, 7.0, kTol);
  EXPECT_GT(d.minimum, *d.parallelDistance) << "minimum distance differs from plane separation here";
}

TEST(Measure, ParallelStraightEdges) {
  const DistanceResult d =
      distance(segment(gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)), segment(gp_Pnt(20, 3, 4), gp_Pnt(30, 3, 4)));
  ASSERT_TRUE(d.ok);
  ASSERT_TRUE(d.parallelDistance);
  EXPECT_NEAR(*d.parallelDistance, 5.0, kTol);
}

TEST(Measure, CenterToCenterBetweenCylindersAndSpheres) {
  const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(5.0, 10.0).Shape();
  const TopoDS_Shape c1 = faceOfType(cylinder, SurfaceKind::Cylinder);
  const TopoDS_Shape c2 = toWorld(c1, translation(30, 40, 2));
  const DistanceResult axes = distance(c1, c2);
  ASSERT_TRUE(axes.ok);
  ASSERT_TRUE(axes.centerDistance);
  EXPECT_NEAR(*axes.centerDistance, 50.0, kTol) << "parallel axes: distance between the axes";
  EXPECT_NEAR(axes.minimum, 40.0, 1e-6);

  const TopoDS_Shape sphere = faceOfType(BRepPrimAPI_MakeSphere(2.0).Shape(), SurfaceKind::Sphere);
  const DistanceResult spheres = distance(sphere, toWorld(sphere, translation(2, 3, 6)));
  ASSERT_TRUE(spheres.centerDistance);
  EXPECT_NEAR(*spheres.centerDistance, 7.0, kTol);

  // Circular edge center to a cylinder axis: point-to-line.
  const TopoDS_Shape circle = toWorld(edgeOfType(cylinder, CurveKind::Circle), translation(0, 8, 0));
  const DistanceResult mixed = distance(circle, c1);
  ASSERT_TRUE(mixed.centerDistance);
  EXPECT_NEAR(*mixed.centerDistance, 8.0, kTol);
}

TEST(Measure, DistanceFailsCleanlyOnNullInput) {
  const DistanceResult d = distance(TopoDS_Shape(), box(1, 1, 1));
  EXPECT_FALSE(d.ok);
  EXPECT_FALSE(d.error.empty());
}

// --- Angle -------------------------------------------------------------------------

TEST(Measure, AngleBetweenEdgesSharingAVertexIsInterior) {
  const gp_Pnt apex(1, 1, 0);
  // 135 degrees, with edges oriented in different directions relative to the apex.
  const AngleResult a = angle(segment(apex, gp_Pnt(11, 1, 0)), segment(gp_Pnt(-9, 11, 0), apex));
  ASSERT_TRUE(a.ok) << a.error;
  EXPECT_NEAR(degrees(a.angle), 135.0, 1e-9);
  EXPECT_NEAR(degrees(a.supplement), 45.0, 1e-9);
  ASSERT_TRUE(a.apex);
  EXPECT_TRUE(a.apex->IsEqual(apex, kTol));
}

TEST(Measure, AngleBetweenSeparateLinesIsUndirected) {
  const AngleResult a =
      angle(segment(gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)), segment(gp_Pnt(0, 5, 3), gp_Pnt(-10, 15, 3)));
  ASSERT_TRUE(a.ok);
  EXPECT_NEAR(degrees(a.angle), 45.0, 1e-9);
  EXPECT_FALSE(a.apex) << "skew lines do not meet";
}

TEST(Measure, AngleBetweenFacesAndAxes) {
  const TopoDS_Shape solid = box(10, 10, 10);
  const AngleResult faces = angle(face(solid, gp_Pnt(10, 5, 5)), face(solid, gp_Pnt(5, 5, 10)));
  ASSERT_TRUE(faces.ok);
  EXPECT_NEAR(degrees(faces.angle), 90.0, 1e-9);

  const AngleResult opposite = angle(face(solid, gp_Pnt(5, 5, 0)), face(solid, gp_Pnt(5, 5, 10)));
  EXPECT_NEAR(degrees(opposite.angle), 0.0, 1e-9);

  // Cylinder axis tilted 30 degrees from Z against the box's top face normal.
  const gp_Ax2 tilted(gp_Pnt(0, 0, 0), gp_Dir(std::sin(kPi / 6), 0, std::cos(kPi / 6)));
  const TopoDS_Shape cylinder =
      faceOfType(BRepPrimAPI_MakeCylinder(tilted, 2.0, 5.0).Shape(), SurfaceKind::Cylinder);
  const AngleResult axis = angle(cylinder, face(solid, gp_Pnt(5, 5, 10)));
  ASSERT_TRUE(axis.ok);
  EXPECT_NEAR(degrees(axis.angle), 30.0, 1e-9);
}

TEST(Measure, AngleRejectsEntitiesWithoutDirection) {
  const AngleResult a = angle(BRepBuilderAPI_MakeVertex(gp_Pnt(0, 0, 0)).Shape(), box(1, 1, 1));
  EXPECT_FALSE(a.ok);
  EXPECT_FALSE(a.error.empty());
}

// --- Radius ------------------------------------------------------------------------

TEST(Measure, RadiusOfCirclesCylindersAndSpheres) {
  const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(6.0, 10.0).Shape();
  const auto edge = radius(edgeOfType(cylinder, CurveKind::Circle));
  ASSERT_TRUE(edge);
  EXPECT_NEAR(edge->radius, 6.0, kTol);
  EXPECT_NEAR(edge->diameter, 12.0, kTol);

  const auto side = radius(faceOfType(cylinder, SurfaceKind::Cylinder));
  ASSERT_TRUE(side);
  EXPECT_NEAR(side->diameter, 12.0, kTol);
  EXPECT_NEAR(side->center.Z(), 5.0, 1e-6) << "center is level with the face";
  EXPECT_FALSE(side->isSphere);

  const auto sphere =
      radius(faceOfType(BRepPrimAPI_MakeSphere(gp_Pnt(1, 2, 3), 3.0).Shape(), SurfaceKind::Sphere));
  ASSERT_TRUE(sphere);
  EXPECT_TRUE(sphere->isSphere);
  EXPECT_TRUE(sphere->center.IsEqual(gp_Pnt(1, 2, 3), kTol));

  EXPECT_FALSE(radius(face(box(1, 1, 1), gp_Pnt(0.5, 0.5, 1))));
  EXPECT_FALSE(radius(segment(gp_Pnt(0, 0, 0), gp_Pnt(1, 0, 0))));
}

// --- Mass properties ---------------------------------------------------------------

TEST(Measure, MassPropertiesOfABox) {
  const MassProperties m = massProperties({box(10, 20, 30)});
  ASSERT_TRUE(m.ok);
  EXPECT_TRUE(m.hasVolume);
  EXPECT_EQ(m.solidCount, 1);
  EXPECT_NEAR(m.volume, 6000.0, 1e-6);
  EXPECT_NEAR(m.area, 2200.0, 1e-6);
  EXPECT_TRUE(m.centroid.IsEqual(gp_Pnt(5, 10, 15), 1e-9));
  // Unit density: Ixx = m (b^2 + c^2) / 12 about the centroid.
  EXPECT_NEAR(m.inertia(1, 1), 6000.0 * (400 + 900) / 12.0, 1e-3);
  EXPECT_NEAR(m.inertia(2, 2), 6000.0 * (100 + 900) / 12.0, 1e-3);
  EXPECT_NEAR(m.inertia(3, 3), 6000.0 * (100 + 400) / 12.0, 1e-3);
  EXPECT_NEAR(m.inertia(1, 2), 0.0, 1e-6);
  EXPECT_TRUE(m.boxMin.IsEqual(gp_Pnt(0, 0, 0), 1e-6));
  EXPECT_TRUE(m.boxMax.IsEqual(gp_Pnt(10, 20, 30), 1e-6));
}

TEST(Measure, MassPropertiesOfSeveralSolidsCombine) {
  const MassProperties m = massProperties({box(10, 10, 10), toWorld(box(10, 10, 10), translation(20, 0, 0))});
  ASSERT_TRUE(m.ok);
  EXPECT_EQ(m.solidCount, 2);
  EXPECT_NEAR(m.volume, 2000.0, 1e-6);
  EXPECT_TRUE(m.centroid.IsEqual(gp_Pnt(15, 5, 5), 1e-9));
}

TEST(Measure, SurfaceOnlySelectionHasNoVolume) {
  const MassProperties m = massProperties({face(box(10, 20, 30), gp_Pnt(5, 10, 30))});
  ASSERT_TRUE(m.ok);
  EXPECT_FALSE(m.hasVolume);
  EXPECT_NEAR(m.area, 200.0, 1e-6);
  EXPECT_TRUE(m.centroid.IsEqual(gp_Pnt(5, 10, 30), 1e-9));
}

TEST(Measure, OrientedBoxFollowsARotatedPart) {
  gp_Trsf rotation;
  rotation.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), kPi / 6);
  const MassProperties m = massProperties({toWorld(box(10, 20, 30), TopLoc_Location(rotation))});
  ASSERT_TRUE(m.ok);
  std::array<double, 3> half = m.orientedBox.halfSizes;
  std::sort(half.begin(), half.end());
  EXPECT_NEAR(half[0], 5.0, 1e-3);
  EXPECT_NEAR(half[1], 10.0, 1e-3);
  EXPECT_NEAR(half[2], 15.0, 1e-3);
  // The axis-aligned box of a rotated part is larger.
  EXPECT_GT(m.boxMax.X() - m.boxMin.X(), 10.0 + 1e-3);
}

// --- Transformed instances: every mode must work in world coordinates --------------

TEST(Measure, AllModesRespectInstancePlacement) {
  const TopLoc_Location placement = skewPlacement();
  const TopoDS_Shape prototype = box(10, 20, 30);
  const TopoDS_Shape cylinderPrototype = BRepPrimAPI_MakeCylinder(4.0, 12.0).Shape();

  // Sub-shapes picked on the prototype, placed like an assembly instance.
  const TopoDS_Shape top = toWorld(face(prototype, gp_Pnt(5, 10, 30)), placement);
  const TopoDS_Shape bottom = toWorld(face(prototype, gp_Pnt(5, 10, 0)), placement);
  const TopoDS_Shape side = toWorld(face(prototype, gp_Pnt(10, 10, 15)), placement);

  // Entity info: the normal and centroid move with the instance.
  const EntityInfo topInfo = entityInfo(top);
  EXPECT_TRUE(topInfo.face->normal->IsEqual(gp_Dir(0, 0, 1).Transformed(placement.Transformation()), 1e-9));
  EXPECT_TRUE(topInfo.face->centroid.IsEqual(transformed(gp_Pnt(5, 10, 30), placement), 1e-6));
  EXPECT_NEAR(topInfo.face->area, 200.0, 1e-6);

  // Distance: invariant under rigid motion.
  const DistanceResult d = distance(top, bottom);
  ASSERT_TRUE(d.ok);
  EXPECT_NEAR(d.minimum, 30.0, 1e-6);
  ASSERT_TRUE(d.parallelDistance);
  EXPECT_NEAR(*d.parallelDistance, 30.0, 1e-6);

  // Angle between faces.
  const AngleResult a = angle(top, side);
  ASSERT_TRUE(a.ok);
  EXPECT_NEAR(degrees(a.angle), 90.0, 1e-9);

  // Radius and its center in world coordinates.
  const auto r = radius(toWorld(faceOfType(cylinderPrototype, SurfaceKind::Cylinder), placement));
  ASSERT_TRUE(r);
  EXPECT_NEAR(r->radius, 4.0, kTol);
  EXPECT_TRUE(r->center.IsEqual(transformed(gp_Pnt(0, 0, 6), placement), 1e-6));

  // Mass properties: the centroid moves, volume and principal moments don't.
  const MassProperties m = massProperties({toWorld(prototype, placement)});
  ASSERT_TRUE(m.ok);
  EXPECT_NEAR(m.volume, 6000.0, 1e-6);
  EXPECT_TRUE(m.centroid.IsEqual(transformed(gp_Pnt(5, 10, 15), placement), 1e-6));
  std::array<double, 3> moments = m.principalMoments;
  std::sort(moments.begin(), moments.end());
  EXPECT_NEAR(moments[0], 6000.0 * (100 + 400) / 12.0, 1e-2);
  EXPECT_NEAR(moments[1], 6000.0 * (100 + 900) / 12.0, 1e-2);
  EXPECT_NEAR(moments[2], 6000.0 * (400 + 900) / 12.0, 1e-2);

  // Two instances of the same part: center-to-center between their cylinder axes.
  const TopoDS_Shape c1 = toWorld(faceOfType(cylinderPrototype, SurfaceKind::Cylinder), placement);
  const TopoDS_Shape c2 =
      toWorld(faceOfType(cylinderPrototype, SurfaceKind::Cylinder), placement * translation(6, 8, 0));
  const DistanceResult axes = distance(c1, c2);
  ASSERT_TRUE(axes.centerDistance);
  EXPECT_NEAR(*axes.centerDistance, 10.0, 1e-6);
}
