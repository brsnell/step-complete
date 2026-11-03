// Interactive measurement pipeline: picks -> formatted result + witness geometry.

#include "core/Measurement.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>

#include <gtest/gtest.h>

using namespace sc::core;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string detail(const Measurement& m, const std::string& key) {
  for (const auto& [k, v] : m.details) {
    if (k == key) {
      return v;
    }
  }
  return "<missing " + key + ">";
}

TopoDS_Shape boxFace(const gp_Pnt& near) {
  const TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
  TopoDS_Shape best;
  double bestDistance = 1e100;
  for (TopExp_Explorer it(box, TopAbs_FACE); it.More(); it.Next()) {
    const auto info = measure::entityInfo(it.Current());
    const double d = info.face->centroid.Distance(near);
    if (d < bestDistance) {
      bestDistance = d;
      best = it.Current();
    }
  }
  return best;
}

TopoDS_Shape cylinderFace() {
  const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(5.0, 10.0).Shape();
  for (TopExp_Explorer it(cylinder, TopAbs_FACE); it.More(); it.Next()) {
    if (measure::entityInfo(it.Current()).face->surface == measure::SurfaceKind::Cylinder) {
      return it.Current();
    }
  }
  return {};
}

Pick pick(const TopoDS_Shape& shape, const gp_Pnt& at, std::string owner = {}) {
  return Pick{shape, at, std::move(owner)};
}

} // namespace

TEST(Measurement, PicksRequiredPerMode) {
  EXPECT_EQ(picksRequired(MeasureMode::EntityInfo), 1);
  EXPECT_EQ(picksRequired(MeasureMode::Radius), 1);
  EXPECT_EQ(picksRequired(MeasureMode::Distance), 2);
  EXPECT_EQ(picksRequired(MeasureMode::PointToPoint), 2);
  EXPECT_EQ(picksRequired(MeasureMode::Angle), 2);
}

TEST(Measurement, RejectsWrongPickCount) {
  const Measurement m =
      computeMeasurement(MeasureMode::Distance, {pick(boxFace({5, 10, 30}), {5, 10, 30})}, {});
  EXPECT_FALSE(m.ok);
  EXPECT_FALSE(m.error.empty());
}

TEST(Measurement, DistanceBetweenFacesInInches) {
  MeasureDisplayOptions options;
  options.lengthUnit = LengthUnit::Inch;
  options.decimals = 4;
  const Measurement m = computeMeasurement(
      MeasureMode::Distance,
      {pick(boxFace({5, 10, 0}), {5, 10, 0}, "Box"), pick(boxFace({5, 10, 30}), {5, 10, 30})}, options);
  ASSERT_TRUE(m.ok) << m.error;
  EXPECT_EQ(m.headline, "1.1811 in"); // 30 mm
  EXPECT_EQ(detail(m, "Parallel distance"), "1.1811 in");
  ASSERT_EQ(m.entities.size(), 2u);
  EXPECT_EQ(m.entities[0], "Face (Plane) on Box");
  EXPECT_EQ(m.entities[1], "Face (Plane)");
  // Witness: a segment between the closest points, labeled with the value.
  ASSERT_FALSE(m.witness.polylines.empty());
  EXPECT_NEAR(m.witness.polylines[0][0].Distance(m.witness.polylines[0][1]), 30.0, 1e-6);
  EXPECT_EQ(m.witness.label, m.headline);
}

TEST(Measurement, PointToPointSnapsToVerticesMidpointsAndCenters) {
  const TopoDS_Shape edge = BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)).Edge();
  // Near the midpoint: snaps to it. Far from any snap point: projects onto the edge.
  EXPECT_TRUE(snapPoint(edge, gp_Pnt(5.2, 0.3, 0), 0.5).IsEqual(gp_Pnt(5, 0, 0), 1e-9));
  EXPECT_TRUE(snapPoint(edge, gp_Pnt(9.9, 0.1, 0), 0.5).IsEqual(gp_Pnt(10, 0, 0), 1e-9));
  EXPECT_TRUE(snapPoint(edge, gp_Pnt(3, 1, 0), 0.5).IsEqual(gp_Pnt(3, 0, 0), 1e-6));

  const TopoDS_Shape circle =
      BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(1, 2, 3), gp_Dir(0, 0, 1)), 4)).Edge();
  EXPECT_TRUE(snapPoint(circle, gp_Pnt(5, 2, 3), 0.5).IsEqual(gp_Pnt(1, 2, 3), 1e-9))
      << "circles snap to center";

  EXPECT_EQ(snap(edge, gp_Pnt(5.2, 0.3, 0), 0.5).kind, SnapKind::Midpoint);
  EXPECT_EQ(snap(edge, gp_Pnt(9.9, 0.1, 0), 0.5).kind, SnapKind::Endpoint);
  EXPECT_EQ(snap(edge, gp_Pnt(3, 1, 0), 0.5).kind, SnapKind::OnEdge);
  EXPECT_EQ(snap(circle, gp_Pnt(5, 2, 3), 0.5).kind, SnapKind::Center);
  EXPECT_EQ(snap(boxFace({5, 10, 30}), gp_Pnt(4, 8, 30.05), 0.5, false).kind, SnapKind::OnFace);
  EXPECT_NEAR(snap(boxFace({5, 10, 30}), gp_Pnt(4, 8, 30.05), 0.5, false).point.Z(), 30.05, 1e-12)
      << "no projection when hovering";

  // A click slightly off a face (mesh vs exact surface) lands on the exact face.
  EXPECT_NEAR(snapPoint(boxFace({5, 10, 30}), gp_Pnt(4, 8, 30.05), 0.5).Z(), 30.0, 1e-9);

  const Measurement m = computeMeasurement(
      MeasureMode::PointToPoint, {pick(edge, gp_Pnt(0.1, 0, 0)), pick(circle, gp_Pnt(5, 2, 3))}, {}, 0.5);
  ASSERT_TRUE(m.ok);
  EXPECT_EQ(m.headline, "3.742 mm"); // |(1,2,3)|
  EXPECT_EQ(detail(m, "\xCE\x94Z"), "3.000 mm");
}

TEST(Measurement, AngleBetweenEdgesDrawsAnArcAtTheApex) {
  const gp_Pnt apex(0, 0, 0);
  const TopoDS_Shape e1 = BRepBuilderAPI_MakeEdge(apex, gp_Pnt(10, 0, 0)).Edge();
  const TopoDS_Shape e2 = BRepBuilderAPI_MakeEdge(apex, gp_Pnt(0, 10, 0)).Edge();
  const Measurement m =
      computeMeasurement(MeasureMode::Angle, {pick(e1, gp_Pnt(8, 0, 0)), pick(e2, gp_Pnt(0, 5, 0))}, {});
  ASSERT_TRUE(m.ok) << m.error;
  EXPECT_EQ(m.headline, "90.00\xC2\xB0");
  EXPECT_EQ(detail(m, "Supplement"), "90.00\xC2\xB0");
  ASSERT_EQ(m.witness.polylines.size(), 2u);
  const auto& arc = m.witness.polylines[1];
  EXPECT_NEAR(arc.front().Distance(apex), 3.0, 1e-9) << "arc radius is 60% of the nearer pick";
  EXPECT_NEAR(arc.back().Distance(apex), 3.0, 1e-9);
  EXPECT_NEAR(m.witness.labelAnchor.Distance(apex), 3.0, 1e-9);
}

TEST(Measurement, AngleInRadians) {
  MeasureDisplayOptions options;
  options.angleUnit = AngleUnit::Radian;
  options.angleDecimals = 4;
  const Measurement m = computeMeasurement(
      MeasureMode::Angle,
      {pick(boxFace({10, 10, 15}), {10, 10, 15}), pick(boxFace({5, 10, 30}), {5, 10, 30})}, options);
  ASSERT_TRUE(m.ok);
  EXPECT_EQ(m.headline, "1.5708 rad");
}

TEST(Measurement, RadiusOfCylinderWithCircleWitness) {
  const Measurement m = computeMeasurement(MeasureMode::Radius, {pick(cylinderFace(), gp_Pnt(5, 0, 7))}, {});
  ASSERT_TRUE(m.ok) << m.error;
  EXPECT_EQ(detail(m, "Radius"), "5.000 mm");
  EXPECT_EQ(detail(m, "Diameter"), "10.000 mm");
  EXPECT_EQ(m.witness.label, "R 5.000 mm");
  ASSERT_EQ(m.witness.polylines.size(), 2u);
  // The radius line points from the axis toward the click.
  const auto& radiusLine = m.witness.polylines[0];
  EXPECT_TRUE(radiusLine[1].IsEqual(gp_Pnt(5, 0, 5), 1e-6));
  for (const gp_Pnt& p : m.witness.polylines[1]) {
    EXPECT_NEAR(std::hypot(p.X(), p.Y()), 5.0, 1e-9);
  }
}

TEST(Measurement, RadiusRejectsPlanarFaces) {
  const Measurement m =
      computeMeasurement(MeasureMode::Radius, {pick(boxFace({5, 10, 30}), {5, 10, 30})}, {});
  EXPECT_FALSE(m.ok);
  EXPECT_FALSE(m.error.empty());
}

TEST(Measurement, EntityInfoOfCircularEdge) {
  const TopoDS_Shape arc =
      BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 10), 0.0, kPi / 2).Edge();
  const Measurement m = computeMeasurement(MeasureMode::EntityInfo, {pick(arc, gp_Pnt(7, 7, 0))}, {});
  ASSERT_TRUE(m.ok);
  EXPECT_EQ(m.headline, "15.708 mm");
  EXPECT_EQ(detail(m, "Type"), "Circle");
  EXPECT_EQ(detail(m, "Radius"), "10.000 mm");
  EXPECT_EQ(detail(m, "Arc angle"), "90.00\xC2\xB0");
  EXPECT_EQ(detail(m, "Center"), "0.000, 0.000, 0.000 mm");
  EXPECT_EQ(m.witness.markers.size(), 2u) << "open edges mark their ends";
}

TEST(Measurement, EntityInfoOfFace) {
  const Measurement m =
      computeMeasurement(MeasureMode::EntityInfo, {pick(boxFace({5, 10, 30}), {5, 10, 30})}, {});
  ASSERT_TRUE(m.ok);
  EXPECT_EQ(m.headline, "200.000 mm\xC2\xB2");
  EXPECT_EQ(detail(m, "Normal"), "0.0000, 0.0000, 1.0000");
  // OCCT's default tolerance (1e-7 mm) is below the display precision.
  EXPECT_EQ(detail(m, "Tolerance"), "< 0.001 mm");
}

TEST(Measurement, ClipboardAndCsvExport) {
  const Measurement m = computeMeasurement(
      MeasureMode::Distance,
      {pick(boxFace({5, 10, 0}), {5, 10, 0}, "Plate, top"), pick(boxFace({5, 10, 30}), {5, 10, 30})}, {});
  ASSERT_TRUE(m.ok);
  const std::string text = toClipboardText(m);
  EXPECT_EQ(text.rfind("Distance: 30.000 mm\n", 0), 0u) << text;
  EXPECT_NE(text.find("  Face (Plane) on Plate, top\n"), std::string::npos);

  const std::string csv = toCsv({m, m});
  EXPECT_EQ(csv.rfind("Mode,Value,Entities,Details\n", 0), 0u);
  // Fields containing commas are quoted.
  EXPECT_NE(csv.find("Distance,30.000 mm,\"Face (Plane) on Plate, top; Face (Plane)\","), std::string::npos)
      << csv;
  EXPECT_EQ(std::count(csv.begin(), csv.end(), '\n'), 3);
}
