// Interactive measurement pipeline: picks -> formatted result + witness geometry.

#include "core/Measure.h"
#include "core/Measurement.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <gp_Trsf.hxx>

#include <gtest/gtest.h>

using namespace sc::core;

namespace {

constexpr double Pi = 3.14159265358979323846;

std::string detail(const Measurement& measurement, const std::string& key) {
  for (const auto& [k, v] : measurement.details) {
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
    const double distance = info.face->centroid.Distance(near);
    if (distance < bestDistance) {
      bestDistance = distance;
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
  const Measurement measurement =
      computeMeasurement(MeasureMode::Distance, {pick(boxFace({5, 10, 30}), {5, 10, 30})}, {});
  EXPECT_FALSE(measurement.ok);
  EXPECT_FALSE(measurement.error.empty());
}

TEST(Measurement, DistanceBetweenFacesInInches) {
  MeasureDisplayOptions options;
  options.lengthUnit = LengthUnit::Inch;
  options.decimals = 4;
  const Measurement measurement = computeMeasurement(
      MeasureMode::Distance,
      {pick(boxFace({5, 10, 0}), {5, 10, 0}, "Box"), pick(boxFace({5, 10, 30}), {5, 10, 30})}, options);
  ASSERT_TRUE(measurement.ok) << measurement.error;
  EXPECT_EQ(measurement.headline, "1.1811 in"); // 30 mm
  EXPECT_EQ(detail(measurement, "Parallel distance"), "1.1811 in");
  ASSERT_EQ(measurement.entities.size(), 2u);
  EXPECT_EQ(measurement.entities[0], "Face (Plane) on Box");
  EXPECT_EQ(measurement.entities[1], "Face (Plane)");
  // Witness: a segment between the closest points, labeled with the value.
  ASSERT_FALSE(measurement.witness.polylines.empty());
  EXPECT_NEAR(measurement.witness.polylines[0][0].Distance(measurement.witness.polylines[0][1]), 30.0, 1e-6);
  EXPECT_EQ(measurement.witness.label, measurement.headline);
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

  const Measurement measurement = computeMeasurement(
      MeasureMode::PointToPoint, {pick(edge, gp_Pnt(0.1, 0, 0)), pick(circle, gp_Pnt(5, 2, 3))}, {}, 0.5);
  ASSERT_TRUE(measurement.ok);
  EXPECT_EQ(measurement.headline, "3.742 mm"); // |(1,2,3)|
  EXPECT_EQ(detail(measurement, "\xCE\x94Z"), "3.000 mm");
}

TEST(Measurement, AngleBetweenEdgesDrawsAnArcAtTheApex) {
  const gp_Pnt apex(0, 0, 0);
  const TopoDS_Shape edge1 = BRepBuilderAPI_MakeEdge(apex, gp_Pnt(10, 0, 0)).Edge();
  const TopoDS_Shape edge2 = BRepBuilderAPI_MakeEdge(apex, gp_Pnt(0, 10, 0)).Edge();
  const Measurement measurement = computeMeasurement(
      MeasureMode::Angle, {pick(edge1, gp_Pnt(8, 0, 0)), pick(edge2, gp_Pnt(0, 5, 0))}, {});
  ASSERT_TRUE(measurement.ok) << measurement.error;
  EXPECT_EQ(measurement.headline, "90.00\xC2\xB0");
  EXPECT_EQ(detail(measurement, "Supplement"), "90.00\xC2\xB0");
  ASSERT_EQ(measurement.witness.polylines.size(), 2u);
  const auto& arc = measurement.witness.polylines[1];
  EXPECT_NEAR(arc.front().Distance(apex), 3.0, 1e-9) << "arc radius is 60% of the nearer pick";
  EXPECT_NEAR(arc.back().Distance(apex), 3.0, 1e-9);
  EXPECT_NEAR(measurement.witness.labelAnchor.Distance(apex), 3.0, 1e-9);
}

TEST(Measurement, AngleInRadians) {
  MeasureDisplayOptions options;
  options.angleUnit = AngleUnit::Radian;
  options.angleDecimals = 4;
  const Measurement measurement = computeMeasurement(
      MeasureMode::Angle,
      {pick(boxFace({10, 10, 15}), {10, 10, 15}), pick(boxFace({5, 10, 30}), {5, 10, 30})}, options);
  ASSERT_TRUE(measurement.ok);
  EXPECT_EQ(measurement.headline, "1.5708 rad");
}

TEST(Measurement, RadiusOfCylinderWithCircleWitness) {
  const Measurement measurement =
      computeMeasurement(MeasureMode::Radius, {pick(cylinderFace(), gp_Pnt(5, 0, 7))}, {});
  ASSERT_TRUE(measurement.ok) << measurement.error;
  EXPECT_EQ(detail(measurement, "Radius"), "5.000 mm");
  EXPECT_EQ(detail(measurement, "Diameter"), "10.000 mm");
  EXPECT_EQ(measurement.witness.label, "R 5.000 mm");
  ASSERT_EQ(measurement.witness.polylines.size(), 2u);
  // The radius line points from the axis toward the click.
  const auto& radiusLine = measurement.witness.polylines[0];
  EXPECT_TRUE(radiusLine[1].IsEqual(gp_Pnt(5, 0, 5), 1e-6));
  for (const gp_Pnt& point : measurement.witness.polylines[1]) {
    EXPECT_NEAR(std::hypot(point.X(), point.Y()), 5.0, 1e-9);
  }
}

TEST(Measurement, RadiusRejectsPlanarFaces) {
  const Measurement measurement =
      computeMeasurement(MeasureMode::Radius, {pick(boxFace({5, 10, 30}), {5, 10, 30})}, {});
  EXPECT_FALSE(measurement.ok);
  EXPECT_FALSE(measurement.error.empty());
}

TEST(Measurement, EntityInfoOfCircularEdge) {
  const TopoDS_Shape arc =
      BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 10), 0.0, Pi / 2).Edge();
  const Measurement measurement =
      computeMeasurement(MeasureMode::EntityInfo, {pick(arc, gp_Pnt(7, 7, 0))}, {});
  ASSERT_TRUE(measurement.ok);
  EXPECT_EQ(measurement.headline, "15.708 mm");
  EXPECT_EQ(detail(measurement, "Type"), "Circle");
  EXPECT_EQ(detail(measurement, "Radius"), "10.000 mm");
  EXPECT_EQ(detail(measurement, "Arc angle"), "90.00\xC2\xB0");
  EXPECT_EQ(detail(measurement, "Center"), "0.000, 0.000, 0.000 mm");
  EXPECT_EQ(measurement.witness.markers.size(), 2u) << "open edges mark their ends";
}

TEST(Measurement, EntityInfoOfFace) {
  const Measurement measurement =
      computeMeasurement(MeasureMode::EntityInfo, {pick(boxFace({5, 10, 30}), {5, 10, 30})}, {});
  ASSERT_TRUE(measurement.ok);
  EXPECT_EQ(measurement.headline, "200.000 mm\xC2\xB2");
  EXPECT_EQ(detail(measurement, "Normal"), "0.0000, 0.0000, 1.0000");
  // OCCT's default tolerance (1e-7 mm) is below the display precision.
  EXPECT_EQ(detail(measurement, "Tolerance"), "< 0.001 mm");
}

TEST(Measurement, ClipboardAndCsvExport) {
  const Measurement measurement = computeMeasurement(
      MeasureMode::Distance,
      {pick(boxFace({5, 10, 0}), {5, 10, 0}, "Plate, top"), pick(boxFace({5, 10, 30}), {5, 10, 30})}, {});
  ASSERT_TRUE(measurement.ok);
  const std::string text = toClipboardText(measurement);
  EXPECT_EQ(text.rfind("Distance: 30.000 mm\n", 0), 0u) << text;
  EXPECT_NE(text.find("  Face (Plane) on Plate, top\n"), std::string::npos);

  const std::string csv = toCsv({measurement, measurement});
  EXPECT_EQ(csv.rfind("Mode,Value,Entities,Details\n", 0), 0u);
  // Fields containing commas are quoted.
  EXPECT_NE(csv.find("Distance,30.000 mm,\"Face (Plane) on Plate, top; Face (Plane)\","), std::string::npos)
      << csv;
  EXPECT_EQ(std::count(csv.begin(), csv.end(), '\n'), 3);
}

// --- Mass properties ---------------------------------------------------------------

namespace {

MassItem boxItem(const std::string& name, const gp_Vec& offset = gp_Vec(0, 0, 0)) {
  MassItem item;
  gp_Trsf move;
  move.SetTranslation(offset);
  item.location = TopLoc_Location(move);
  item.worldShape = BRepPrimAPI_MakeBox(10, 20, 30).Shape().Moved(item.location);
  item.name = name;
  return item;
}

ValidationProperties exactBoxProperties() {
  ValidationProperties declared;
  declared.volume = 6000.0;
  declared.area = 2200.0;
  declared.centroid = gp_Pnt(5, 10, 15); // prototype coordinates
  return declared;
}

bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

} // namespace

TEST(MassProperties, ModeMetadata) {
  EXPECT_STREQ(toString(MeasureMode::MassProperties), "Mass properties");
  EXPECT_EQ(picksRequired(MeasureMode::MassProperties), 0);
  const Measurement viaPicks = computeMeasurement(MeasureMode::MassProperties, {}, {});
  EXPECT_FALSE(viaPicks.ok);
  EXPECT_FALSE(viaPicks.error.empty());
}

TEST(MassProperties, SingleBox) {
  const Measurement measurement = computeMassProperties({boxItem("Plate")}, {});
  ASSERT_TRUE(measurement.ok) << measurement.error;
  EXPECT_EQ(measurement.mode, MeasureMode::MassProperties);
  EXPECT_EQ(measurement.headline, "6000.000 mm\xC2\xB3");
  EXPECT_EQ(detail(measurement, "Solids"), "1");
  EXPECT_EQ(detail(measurement, "Volume"), "6000.000 mm\xC2\xB3");
  EXPECT_EQ(detail(measurement, "Surface area"), "2200.000 mm\xC2\xB2");
  EXPECT_EQ(detail(measurement, "Centroid"), "5.000, 10.000, 15.000 mm");
  EXPECT_EQ(detail(measurement, "Bounding box size"), "10.000 \xC3\x97 20.000 \xC3\x97 30.000 mm");
  EXPECT_EQ(detail(measurement, "Bounding box min"), "0.000, 0.000, 0.000 mm");
  EXPECT_EQ(detail(measurement, "Bounding box max"), "10.000, 20.000, 30.000 mm");
  EXPECT_EQ(detail(measurement, "Oriented box size"), "30.000 \xC3\x97 20.000 \xC3\x97 10.000 mm");

  // Unit density: m/12 (b^2 + c^2) for each axis, in mm^5.
  const std::string moments = detail(measurement, "Principal moments (unit density)");
  for (const char* expected : {"250000 mm\xE2\x81\xB5", "500000 mm\xE2\x81\xB5", "650000 mm\xE2\x81\xB5"}) {
    EXPECT_TRUE(contains(moments, expected)) << moments;
  }
  EXPECT_EQ(measurement.entities, std::vector<std::string>{"Plate"});
}

TEST(MassProperties, WitnessIsTheBoundingBoxWithCentroidMarker) {
  const Measurement measurement = computeMassProperties({boxItem("Plate")}, {});
  ASSERT_TRUE(measurement.ok);
  EXPECT_EQ(measurement.witness.polylines.size(), 12u);
  for (const auto& edge : measurement.witness.polylines) {
    ASSERT_EQ(edge.size(), 2u);
    // Each edge of an axis-aligned box changes exactly one coordinate.
    const int changed = (std::abs(edge[0].X() - edge[1].X()) > 1e-9) +
                        (std::abs(edge[0].Y() - edge[1].Y()) > 1e-9) +
                        (std::abs(edge[0].Z() - edge[1].Z()) > 1e-9);
    EXPECT_EQ(changed, 1);
  }
  ASSERT_EQ(measurement.witness.markers.size(), 1u);
  EXPECT_NEAR(measurement.witness.markers[0].Distance(gp_Pnt(5, 10, 15)), 0.0, 1e-9);
  EXPECT_NEAR(measurement.witness.labelAnchor.Distance(gp_Pnt(5, 10, 15)), 0.0, 1e-9);
  EXPECT_EQ(measurement.witness.label, measurement.headline);
}

TEST(MassProperties, TwoBoxesAreCombined) {
  const Measurement measurement = computeMassProperties({boxItem("A"), boxItem("B", gp_Vec(100, 0, 0))}, {});
  ASSERT_TRUE(measurement.ok) << measurement.error;
  EXPECT_EQ(measurement.headline, "12000.000 mm\xC2\xB3");
  EXPECT_EQ(detail(measurement, "Solids"), "2");
  EXPECT_EQ(detail(measurement, "Surface area"), "4400.000 mm\xC2\xB2");
  EXPECT_EQ(detail(measurement, "Centroid"), "55.000, 10.000, 15.000 mm");
  EXPECT_EQ(detail(measurement, "Bounding box size"), "110.000 \xC3\x97 20.000 \xC3\x97 30.000 mm");
  EXPECT_EQ(measurement.entities.size(), 2u);
}

TEST(MassProperties, EntityListIsCapped) {
  std::vector<MassItem> items;
  for (int i = 0; i < 13; ++i) {
    items.push_back(boxItem("Box " + std::to_string(i), gp_Vec(100.0 * i, 0, 0)));
  }
  const Measurement measurement = computeMassProperties(items, {});
  ASSERT_TRUE(measurement.ok);
  ASSERT_EQ(measurement.entities.size(), 11u);
  EXPECT_EQ(measurement.entities.front(), "Box 0");
  EXPECT_EQ(measurement.entities.back(), "and 3 more");
}

TEST(MassProperties, MatchingDeclaredPropertiesValidate) {
  MassItem item = boxItem("Plate", gp_Vec(100, 0, 0)); // placed: the declared centroid must move too
  item.declared = exactBoxProperties();
  const Measurement measurement = computeMassProperties({item}, {});
  ASSERT_TRUE(measurement.ok) << measurement.error;
  EXPECT_EQ(detail(measurement, "Declared volume"), "6000.000 mm\xC2\xB3");
  EXPECT_EQ(detail(measurement, "Declared area"), "2200.000 mm\xC2\xB2");
  EXPECT_EQ(detail(measurement, "Declared centroid"), "105.000, 10.000, 15.000 mm");
  EXPECT_EQ(detail(measurement, "Validation"), "Matches");
}

TEST(MassProperties, DeclaredVolumeOffByOnePercentDiffers) {
  MassItem item = boxItem("Plate");
  item.declared = exactBoxProperties();
  item.declared->volume = 6000.0 / 1.01; // the computed volume is 1% above the claim
  const Measurement measurement = computeMassProperties({item}, {});
  ASSERT_TRUE(measurement.ok);
  const std::string verdict = detail(measurement, "Validation");
  EXPECT_EQ(verdict.rfind("Differs: ", 0), 0u) << verdict;
  EXPECT_TRUE(contains(verdict, "volume +1.00%")) << verdict;
  EXPECT_FALSE(contains(verdict, "area")) << verdict;
  EXPECT_FALSE(contains(verdict, "centroid")) << verdict;
}

TEST(MassProperties, DeclaredCentroidInWrongSpaceDiffers) {
  // Forgetting the instance placement is exactly the mistake the comparison must expose.
  MassItem item = boxItem("Plate", gp_Vec(100, 0, 0));
  item.declared = exactBoxProperties();
  item.location = TopLoc_Location();
  const Measurement measurement = computeMassProperties({item}, {});
  ASSERT_TRUE(measurement.ok);
  EXPECT_TRUE(contains(detail(measurement, "Validation"), "centroid 100.000 mm off"))
      << detail(measurement, "Validation");
}

TEST(MassProperties, ValidationOnlyForASingleInstance) {
  MassItem a = boxItem("A");
  MassItem b = boxItem("B", gp_Vec(100, 0, 0));
  a.declared = exactBoxProperties();
  const Measurement measurement = computeMassProperties({a, b}, {});
  ASSERT_TRUE(measurement.ok);
  EXPECT_EQ(detail(measurement, "Validation"), "<missing Validation>");
  EXPECT_EQ(detail(measurement, "Declared volume"), "<missing Declared volume>");

  // A single item without claims has nothing to compare either.
  const Measurement none = computeMassProperties({boxItem("C")}, {});
  EXPECT_EQ(detail(none, "Validation"), "<missing Validation>");
}

TEST(MassProperties, SurfaceOnlySelectionReportsArea) {
  MassItem item;
  item.name = "Sheet";
  item.worldShape = boxFace({5, 10, 30});
  const Measurement measurement = computeMassProperties({item}, {});
  ASSERT_TRUE(measurement.ok) << measurement.error;
  EXPECT_EQ(measurement.headline, "200.000 mm\xC2\xB2");
  EXPECT_EQ(detail(measurement, "Solids"), "0");
  EXPECT_EQ(detail(measurement, "Volume"), "<missing Volume>");
  EXPECT_EQ(detail(measurement, "Principal moments (unit density)"),
            "<missing Principal moments (unit density)>");
  EXPECT_EQ(detail(measurement, "Centroid (surface)"), "5.000, 10.000, 30.000 mm");
}

TEST(MassProperties, NothingToMeasureIsAnError) {
  EXPECT_FALSE(computeMassProperties({}, {}).ok);
  MassItem empty;
  empty.name = "Empty";
  const Measurement measurement = computeMassProperties({empty}, {});
  EXPECT_FALSE(measurement.ok);
  EXPECT_FALSE(measurement.error.empty());
}

TEST(MassProperties, InchDisplayOptions) {
  MeasureDisplayOptions inches;
  inches.lengthUnit = LengthUnit::Inch;
  const Measurement measurement = computeMassProperties({boxItem("Plate")}, inches);
  ASSERT_TRUE(measurement.ok);
  EXPECT_EQ(measurement.headline, "0.366 in\xC2\xB3"); // 6000 / 25.4^3
  EXPECT_EQ(detail(measurement, "Surface area"), "3.410 in\xC2\xB2");
  EXPECT_EQ(detail(measurement, "Bounding box size"), "0.394 \xC3\x97 0.787 \xC3\x97 1.181 in");
  // 250000 mm^5 / 25.4^5: keeps its precision instead of rounding to 0.024 (= 0.0236467).
  EXPECT_TRUE(contains(detail(measurement, "Principal moments (unit density)"), "0.0236467 in\xE2\x81\xB5"))
      << detail(measurement, "Principal moments (unit density)");
}

// Material density (docs/designs/material-density.md).
TEST(MassProperties, EachPartCountsWithItsOwnDensity) {
  // Two 10 x 20 x 30 boxes (6000 mm³ each), the second 100 mm along X and three times as dense.
  const MassItem a = boxItem("A");
  const MassItem b = boxItem("B", gp_Vec(100, 0, 0));
  const measure::MassProperties mass =
      measure::massProperties({a.worldShape, b.worldShape}, {1000.0, 3000.0});
  ASSERT_TRUE(mass.ok);
  ASSERT_TRUE(mass.hasMass);
  EXPECT_NEAR(mass.volume, 12000.0, 1e-6);
  EXPECT_NEAR(mass.mass, 0.006 + 0.018, 1e-12);                    // kg
  EXPECT_NEAR(mass.centroid.X(), (5.0 * 1 + 105.0 * 3) / 4, 1e-9); // pulled toward the dense box
  EXPECT_NEAR(mass.centroid.Y(), 10.0, 1e-9);
  // About X through the centre of mass: both boxes are centred on it in Y and Z.
  EXPECT_NEAR(mass.inertia(1, 1), 0.024 * (20.0 * 20.0 + 30.0 * 30.0) / 12.0, 1e-9); // kg mm²

  // Without densities nothing changes: unit density, no mass.
  const measure::MassProperties plain = measure::massProperties({a.worldShape, b.worldShape});
  EXPECT_FALSE(plain.hasMass);
  EXPECT_NEAR(plain.volume, 12000.0, 1e-6);
  EXPECT_NEAR(plain.centroid.X(), 55.0, 1e-9);
}

TEST(MassProperties, MassIsTheHeadlineWhenEveryPartHasAMaterial) {
  MassItem a = boxItem("A");
  MassItem b = boxItem("B", gp_Vec(100, 0, 0));
  a.density = 7850.0;
  a.material = "Steel";
  b.density = 7850.0;
  b.material = "Steel";
  MeasureDisplayOptions options;
  const Measurement kilograms = computeMassProperties({a, b}, options);
  ASSERT_TRUE(kilograms.ok) << kilograms.error;
  EXPECT_EQ(kilograms.headline, "0.094 kg"); // 12000 mm³ of steel
  EXPECT_EQ(detail(kilograms, "Mass"), "0.094 kg");
  EXPECT_EQ(detail(kilograms, "Density"), "7850.0 kg/m\xC2\xB3");
  EXPECT_EQ(detail(kilograms, "Material"), "Steel");
  EXPECT_EQ(detail(kilograms, "Center of mass"), "55.000, 10.000, 15.000 mm");
  EXPECT_NE(detail(kilograms, "Principal moments").find("kg\xC2\xB7mm\xC2\xB2"), std::string::npos);
  EXPECT_EQ(detail(kilograms, "Volume"), "12000.000 mm\xC2\xB3"); // still there

  options.massUnit = MassUnit::Gram;
  EXPECT_EQ(computeMassProperties({a, b}, options).headline, "94.200 g");
  options.massUnit = MassUnit::Pound;
  options.decimals = 4;
  EXPECT_EQ(computeMassProperties({a, b}, options).headline, "0.2077 lb");

  // Different materials: no single density or name to show.
  b.density = 2700.0;
  b.material = "Aluminium";
  const Measurement mixed = computeMassProperties({a, b}, {});
  EXPECT_EQ(mixed.headline, "0.063 kg"); // 0.0471 + 0.0162
  EXPECT_EQ(detail(mixed, "Density"), "<missing Density>");
  EXPECT_EQ(detail(mixed, "Material"), "<missing Material>");
}

TEST(MassProperties, NoMassUntilEveryPartHasAMaterial) {
  MassItem a = boxItem("A");
  MassItem b = boxItem("B", gp_Vec(100, 0, 0));
  a.density = 7850.0;
  const Measurement partial = computeMassProperties({a, b}, {});
  ASSERT_TRUE(partial.ok);
  EXPECT_EQ(partial.headline, "12000.000 mm\xC2\xB3"); // the unit-density result, as before
  EXPECT_EQ(detail(partial, "Mass"), "no material for 1 of 2 parts");
  EXPECT_EQ(detail(partial, "Centroid"), "55.000, 10.000, 15.000 mm");
  EXPECT_EQ(detail(partial, "Center of mass"), "<missing Center of mass>");

  a.density.reset();
  EXPECT_EQ(detail(computeMassProperties({a, b}, {}), "Mass"), "no material set (Measure > Set Material)");
}
