// Section caps for measuring (docs/designs/section-aware-measurement.md): which cap a click
// crossed, and the cap's face.

#include "core/SectionCap.h"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>

#include <gtest/gtest.h>

using namespace sc;

namespace {

// The base plate of the test fixture: 100 x 60 x 10 mm from the origin.
TopoDS_Shape plate() {
  return BRepPrimAPI_MakeBox(100.0, 60.0, 10.0).Shape();
}

// A plane through 'point' cutting away the side 'normal' points to.
core::CutPlane cut(const gp_Pnt& point, const gp_Dir& normal, bool capped = true) {
  return core::CutPlane{gp_Pln(point, normal), capped};
}

double area(const TopoDS_Shape& shape) {
  GProp_GProps props;
  BRepGProp::SurfaceProperties(shape, props);
  return props.Mass();
}

int faceCount(const TopoDS_Shape& shape) {
  int count = 0;
  for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
    ++count;
  }
  return count;
}

const gp_Dir Down(0, 0, -1);

} // namespace

TEST(SectionCap, AClickThroughACappedPlaneCrossesItsCap) {
  // Looking down at the plate cut at z = 5: the ray hits the bottom face behind the cap.
  const std::vector<core::CutPlane> planes = {cut(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1))};
  const auto hit = core::capHit(plate(), gp_Pnt(60, 40, 0), Down, planes);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->plane, 0u);
  EXPECT_NEAR(hit->point.X(), 60.0, 1e-9);
  EXPECT_NEAR(hit->point.Y(), 40.0, 1e-9);
  EXPECT_NEAR(hit->point.Z(), 5.0, 1e-9);
}

TEST(SectionCap, AnUncappedPlaneShowsTheInsideSoTheHitStands) {
  const std::vector<core::CutPlane> planes = {cut(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1), false)};
  EXPECT_FALSE(core::capHit(plate(), gp_Pnt(60, 40, 0), Down, planes).has_value());
}

TEST(SectionCap, ACrossingOutsideThePartIsNoCap) {
  // A hit at x = 150 belongs to something else: the ray crossed the plane outside the plate.
  const std::vector<core::CutPlane> planes = {cut(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1))};
  EXPECT_FALSE(core::capHit(plate(), gp_Pnt(150, 40, 0), Down, planes).has_value());
}

TEST(SectionCap, AHitInFrontOfThePlanesIsNoCap) {
  // Looking up at the bottom face: the cut-away side is behind the hit.
  const std::vector<core::CutPlane> planes = {cut(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1))};
  EXPECT_FALSE(core::capHit(plate(), gp_Pnt(60, 40, 0), gp_Dir(0, 0, 1), planes).has_value());
  // A hit on the plane itself (a face lying in it) is what is seen.
  EXPECT_FALSE(core::capHit(plate(), gp_Pnt(60, 40, 5), Down, planes).has_value());
}

TEST(SectionCap, ThePlaneTheRayEntersLastIsTheVisibleCap) {
  // Two planes cut from above; the lower one (z = 5) is where the kept model starts.
  const std::vector<core::CutPlane> planes = {cut(gp_Pnt(0, 0, 8), gp_Dir(0, 0, 1)),
                                              cut(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1))};
  const auto hit = core::capHit(plate(), gp_Pnt(60, 40, 0), Down, planes);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->plane, 1u);
  EXPECT_NEAR(hit->point.Z(), 5.0, 1e-9);
}

TEST(SectionCap, TheCapFaceIsThePartsCrossSection) {
  const std::vector<core::CutPlane> planes = {cut(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1))};
  const TopoDS_Shape faces = core::capFaces(plate(), planes, 0);
  ASSERT_FALSE(faces.IsNull());
  EXPECT_EQ(faceCount(faces), 1);
  EXPECT_NEAR(area(faces), 100.0 * 60.0, 1e-6);
  EXPECT_TRUE(core::faceAt(faces, gp_Pnt(60, 40, 5)).has_value());
  EXPECT_FALSE(core::faceAt(faces, gp_Pnt(160, 40, 5)).has_value());
}

TEST(SectionCap, OtherPlanesTrimTheCap) {
  // A second plane cuts away x > 50: half of the cross-section is left.
  const std::vector<core::CutPlane> planes = {cut(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1)),
                                              cut(gp_Pnt(50, 0, 0), gp_Dir(1, 0, 0))};
  const TopoDS_Shape faces = core::capFaces(plate(), planes, 0);
  ASSERT_FALSE(faces.IsNull());
  EXPECT_NEAR(area(faces), 50.0 * 60.0, 1e-6);
  EXPECT_FALSE(core::faceAt(faces, gp_Pnt(80, 40, 5)).has_value());
}

TEST(SectionCap, APlaneMissingThePartHasNoCap) {
  const std::vector<core::CutPlane> planes = {cut(gp_Pnt(0, 0, 50), gp_Dir(0, 0, 1))};
  EXPECT_TRUE(core::capFaces(plate(), planes, 0).IsNull());
  EXPECT_TRUE(core::capFaces(plate(), planes, 1).IsNull()); // no such plane
}

TEST(SectionCap, APartWithTwoSolidsGivesTheFaceUnderThePoint) {
  TopoDS_Compound part;
  BRep_Builder builder;
  builder.MakeCompound(part);
  builder.Add(part, BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 10.0, 10.0, 10.0).Shape());
  builder.Add(part, BRepPrimAPI_MakeBox(gp_Pnt(20, 0, 0), 10.0, 10.0, 10.0).Shape());
  const std::vector<core::CutPlane> planes = {cut(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1))};

  const auto hit = core::capHit(part, gp_Pnt(25, 5, 0), Down, planes);
  ASSERT_TRUE(hit.has_value());
  const TopoDS_Shape faces = core::capFaces(part, planes, hit->plane);
  EXPECT_EQ(faceCount(faces), 2);
  const auto face = core::faceAt(faces, hit->point);
  ASSERT_TRUE(face.has_value());
  EXPECT_NEAR(area(*face), 100.0, 1e-6);
  GProp_GProps props;
  BRepGProp::SurfaceProperties(*face, props);
  EXPECT_NEAR(props.CentreOfMass().X(), 25.0, 1e-6);
}

TEST(SectionCap, APartWithoutSolidsHasNoCap) {
  // OCCT caps closed volumes only: a lone face is never capped.
  const TopoDS_Shape sheet =
      BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)), 0, 10, 0, 10).Shape();
  const std::vector<core::CutPlane> planes = {cut(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1))};
  EXPECT_TRUE(core::capFaces(sheet, planes, 0).IsNull());
  EXPECT_FALSE(core::capHit(sheet, gp_Pnt(0, 5, 0), Down, planes).has_value());
}

TEST(SectionCap, PointToPointSnapsOnTheCapsCornersEdgesAndFace) {
  const std::vector<core::CutPlane> planes = {cut(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1))};
  const auto face = core::faceAt(core::capFaces(plate(), planes, 0), gp_Pnt(60, 40, 5));
  ASSERT_TRUE(face.has_value());

  const core::SnapResult corner = core::snapOnCap(*face, gp_Pnt(1, 1, 5), 2.0);
  EXPECT_EQ(corner.kind, core::SnapKind::Vertex);
  EXPECT_TRUE(corner.point.IsEqual(gp_Pnt(0, 0, 5), 1e-6));

  const core::SnapResult midpoint = core::snapOnCap(*face, gp_Pnt(50.5, 1, 5), 2.0);
  EXPECT_EQ(midpoint.kind, core::SnapKind::Midpoint);
  EXPECT_TRUE(midpoint.point.IsEqual(gp_Pnt(50, 0, 5), 1e-6));

  const core::SnapResult onEdge = core::snapOnCap(*face, gp_Pnt(30, 1, 5), 2.0);
  EXPECT_EQ(onEdge.kind, core::SnapKind::OnEdge);
  EXPECT_TRUE(onEdge.point.IsEqual(gp_Pnt(30, 0, 5), 1e-6));

  const core::SnapResult inside = core::snapOnCap(*face, gp_Pnt(30, 30, 5), 2.0);
  EXPECT_EQ(inside.kind, core::SnapKind::OnFace);
  EXPECT_TRUE(inside.point.IsEqual(gp_Pnt(30, 30, 5), 1e-9));
}
