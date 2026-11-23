// Measure > Check Clashes (docs/designs/clash-detection.md): interference, touching and clearance
// between shapes placed on purpose, the bounding-box pass, and the fixture.

#include "TestModels.h"
#include "core/AssemblyIndex.h"
#include "core/Clash.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <TopExp_Explorer.hxx>

#include <gtest/gtest.h>

using namespace sc;

namespace {

TopoDS_Shape box(double x, double y, double z, double dx = 10.0, double dy = 10.0, double dz = 10.0) {
  return BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), dx, dy, dz).Shape();
}

Bnd_Box boxAround(double x0, double x1) {
  Bnd_Box bounds;
  bounds.Update(x0, 0.0, 0.0, x1, 1.0, 1.0);
  return bounds;
}

core::ClashOptions everything(double clearance = 1.0) {
  core::ClashOptions options;
  options.touching = true;
  options.clearance = clearance;
  return options;
}

} // namespace

TEST(Clash, OverlappingBoxesInterfere) {
  // 10 mm cubes overlapping by 2 x 10 x 10.
  const std::optional<core::Clash> clash = core::checkPair(box(0, 0, 0), box(8, 0, 0), {});
  ASSERT_TRUE(clash);
  EXPECT_EQ(clash->kind, core::ClashKind::Interference);
  EXPECT_NEAR(clash->volume, 200.0, 1e-6);
  EXPECT_TRUE(TopExp_Explorer(clash->witness, TopAbs_SOLID).More()); // the shared block

  // A box entirely inside another interferes too.
  const std::optional<core::Clash> inside = core::checkPair(box(0, 0, 0), box(2, 2, 2, 2, 2, 2), {});
  ASSERT_TRUE(inside);
  EXPECT_EQ(inside->kind, core::ClashKind::Interference);
  EXPECT_NEAR(inside->volume, 8.0, 1e-6);
}

TEST(Clash, BoxesSharingAFaceTouch) {
  EXPECT_FALSE(core::checkPair(box(0, 0, 0), box(10, 0, 0), {})); // not asked for
  const std::optional<core::Clash> clash = core::checkPair(box(0, 0, 0), box(10, 0, 0), everything());
  ASSERT_TRUE(clash);
  EXPECT_EQ(clash->kind, core::ClashKind::Touching);
  EXPECT_EQ(clash->volume, 0.0);
  EXPECT_TRUE(TopExp_Explorer(clash->witness, TopAbs_VERTEX).More()); // the contact points
}

TEST(Clash, AGapBelowTheClearanceIsReported) {
  const TopoDS_Shape a = box(0, 0, 0);
  const TopoDS_Shape b = box(10.5, 0, 0);
  EXPECT_FALSE(core::checkPair(a, b, {}));
  EXPECT_FALSE(core::checkPair(a, b, everything(0.4))); // the gap is wider than the clearance
  const std::optional<core::Clash> clash = core::checkPair(a, b, everything(1.0));
  ASSERT_TRUE(clash);
  EXPECT_EQ(clash->kind, core::ClashKind::Clearance);
  EXPECT_NEAR(clash->distance, 0.5, 1e-9);
  EXPECT_TRUE(TopExp_Explorer(clash->witness, TopAbs_EDGE).More()); // the gap line
}

TEST(Clash, TheBoxPassKeepsOnlyOverlappingBoxes) {
  // 0-2 and 1-3 overlap, 5-6 is alone, 2.5-5.5 overlaps 1-3 and 5-6; a void box never pairs.
  const std::vector<Bnd_Box> boxes = {boxAround(0, 2), boxAround(5, 6), boxAround(1, 3), Bnd_Box(),
                                      boxAround(2.5, 5.5)};
  const std::vector<std::pair<int, int>> expected = {{0, 2}, {1, 4}, {2, 4}};
  EXPECT_EQ(core::candidatePairs(boxes), expected);
  EXPECT_TRUE(core::candidatePairs({}).empty());
}

TEST(Clash, ThePinsStandOnThePlate) {
  const core::AssemblyIndex index(test::buildFixtureAssembly());
  // All 5 instances: 4 pins stand on the plate (touching), 20 mm apart from each other.
  const core::ClashResult none = core::findClashes(index, index.roots(), {});
  EXPECT_FALSE(none.cancelled);
  EXPECT_EQ(none.partsChecked, 5);
  EXPECT_EQ(none.pairsChecked, 4); // the box pass drops every pin-pin pair
  EXPECT_EQ(none.clashCount, 0);

  core::ClashOptions touching;
  touching.touching = true;
  const core::ClashResult result = core::findClashes(index, index.roots(), touching);
  EXPECT_EQ(result.clashCount, 4);
  EXPECT_EQ(result.touching, 4);
  for (const core::Clash& clash : result.clashes) {
    EXPECT_EQ(clash.kind, core::ClashKind::Touching);
    EXPECT_EQ(index.node(clash.a).name, "Base plate"); // the plate comes first in the tree
  }

  // A clearance wider than the pins' spacing brings the pin pairs in: 14 mm between the
  // surfaces of pins 20 mm apart (radius 3).
  const core::ClashResult wide = core::findClashes(index, index.roots(), everything(15.0));
  EXPECT_EQ(wide.touching, 4);
  EXPECT_EQ(wide.clearances, 3); // neighbours 20 mm apart along X: 10-30, 30-50, 50-70
  EXPECT_NEAR(wide.clashes.back().distance, 14.0, 1e-6);

  core::CancelToken cancel;
  cancel.cancel();
  EXPECT_TRUE(core::findClashes(index, index.roots(), touching, {}, &cancel).cancelled);
}

TEST(Clash, KindsInPlainWords) {
  EXPECT_STREQ(core::toString(core::ClashKind::Interference), "Interference");
  EXPECT_STREQ(core::toString(core::ClashKind::Failed), "Could not be checked");
}
