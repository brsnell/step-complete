// Measure > Wall Thickness (docs/designs/wall-thickness.md): ray-cast thickness on meshed shapes
// with known walls.

#include "TestModels.h"
#include "core/AssemblyIndex.h"
#include "core/WallThickness.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <gp_Pln.hxx>

#include <gtest/gtest.h>

#include <cmath>

using namespace sc;

namespace {

TopoDS_Shape meshed(const TopoDS_Shape& shape, double deflection = 0.01) {
  BRepMesh_IncrementalMesh mesher(shape, deflection, false, 0.2);
  return shape;
}

// Values at the nodes whose position satisfies the predicate.
template <class Predicate>
std::vector<float> valuesWhere(const core::ThicknessMap& map, Predicate where) {
  std::vector<float> values;
  for (int i = 1; i <= map.mesh->NbNodes(); ++i) {
    if (where(map.mesh->Node(i))) {
      values.push_back(map.thickness[static_cast<size_t>(i - 1)]);
    }
  }
  return values;
}

} // namespace

TEST(WallThickness, APlateIsThinThroughAndLongAcross) {
  const auto map = core::computeThickness(meshed(BRepPrimAPI_MakeBox(10.0, 10.0, 2.0).Shape()));
  ASSERT_TRUE(map);
  EXPECT_EQ(map->valued, map->mesh->NbNodes()); // a closed solid: every point has a value
  EXPECT_NEAR(map->min, 2.0, 1e-6);
  EXPECT_NEAR(map->max, 10.0, 1e-6);
  // Inside the top face (away from its edges) the wall is 2 mm.
  const auto top = valuesWhere(*map, [](const gp_Pnt& p) {
    return std::abs(p.Z() - 2.0) < 1e-9 && p.X() > 0.5 && p.X() < 9.5 && p.Y() > 0.5 && p.Y() < 9.5;
  });
  ASSERT_FALSE(top.empty()); // refinement put points inside the face
  for (const float value : top) {
    EXPECT_NEAR(value, 2.0, 1e-6);
  }
  // On the side at x = 0 the ray crosses the plate's length.
  const auto side = valuesWhere(*map, [](const gp_Pnt& p) {
    return std::abs(p.X()) < 1e-9 && p.Z() > 0.2 && p.Z() < 1.8 && p.Y() > 0.5 && p.Y() < 9.5;
  });
  ASSERT_FALSE(side.empty());
  for (const float value : side) {
    EXPECT_NEAR(value, 10.0, 1e-6);
  }
}

TEST(WallThickness, ASphereReadsItsDiameter) {
  const auto map = core::computeThickness(meshed(BRepPrimAPI_MakeSphere(10.0).Shape()));
  ASSERT_TRUE(map);
  EXPECT_GT(map->valued, map->mesh->NbNodes() * 9 / 10); // the poles may miss
  EXPECT_NEAR(map->min, 20.0, 0.1);
  EXPECT_NEAR(map->max, 20.0, 0.1);
}

TEST(WallThickness, ATubeWallIsTwoMillimetres) {
  const TopoDS_Shape outer = BRepPrimAPI_MakeCylinder(10.0, 30.0).Shape();
  const TopoDS_Shape inner = BRepPrimAPI_MakeCylinder(8.0, 30.0).Shape();
  const TopoDS_Shape tube = meshed(BRepAlgoAPI_Cut(outer, inner).Shape());
  const auto map = core::computeThickness(tube);
  ASSERT_TRUE(map);
  // Points on the curved walls, away from the end faces: the ray goes straight through the wall.
  const auto walls = valuesWhere(*map, [](const gp_Pnt& p) {
    const double r = std::hypot(p.X(), p.Y());
    return p.Z() > 1.0 && p.Z() < 29.0 && (std::abs(r - 10.0) < 0.05 || std::abs(r - 8.0) < 0.05);
  });
  ASSERT_FALSE(walls.empty());
  for (const float value : walls) {
    EXPECT_NEAR(value, 2.0, 0.05);
  }
  EXPECT_NEAR(map->min, 2.0, 0.05);
}

TEST(WallThickness, RefinementAddsPointsUpToTheCap) {
  const TopoDS_Shape plate = meshed(BRepPrimAPI_MakeBox(100.0, 100.0, 1.0).Shape(), 0.1);
  core::ThicknessOptions coarse;
  coarse.refineLevels = 0;
  const auto unrefined = core::computeThickness(plate, coarse);
  const auto refined = core::computeThickness(plate);
  ASSERT_TRUE(unrefined && refined);
  EXPECT_GT(refined->mesh->NbNodes(), unrefined->mesh->NbNodes() * 10);

  core::ThicknessOptions capped;
  capped.maxPoints = 500;
  const auto limited = core::computeThickness(plate, capped);
  ASSERT_TRUE(limited);
  EXPECT_LE(limited->mesh->NbNodes(), 500);
}

TEST(WallThickness, TheReadoutLooksIntoThePart) {
  const auto map = core::computeThickness(meshed(BRepPrimAPI_MakeBox(10.0, 10.0, 2.0).Shape()));
  ASSERT_TRUE(map);
  // Looking down at the top face: the plate is 2 mm thick there.
  const std::optional<double> top = core::thicknessAt(*map, gp_Pnt(3, 4, 2), gp_Dir(0, 0, -1));
  ASSERT_TRUE(top);
  EXPECT_NEAR(*top, 2.0, 1e-6);
  // Looking at the side from -X, at an angle: the ray still goes in along the face's normal.
  const std::optional<double> side = core::thicknessAt(*map, gp_Pnt(0, 5, 1), gp_Dir(1, 0.3, -0.2));
  ASSERT_TRUE(side);
  EXPECT_NEAR(*side, 10.0, 1e-6);
  // Off the part.
  EXPECT_FALSE(core::thicknessAt(*map, gp_Pnt(50, 50, 2), gp_Dir(0, 0, -1)));
}

TEST(WallThickness, ASheetHasNoThickness) {
  const TopoDS_Shape sheet = meshed(BRepBuilderAPI_MakeFace(gp_Pln(), 0, 10, 0, 10).Face());
  const auto map = core::computeThickness(sheet);
  ASSERT_TRUE(map);
  EXPECT_EQ(map->valued, 0);
  EXPECT_TRUE(std::isnan(map->thickness.front()));
  EXPECT_FALSE(core::computeThickness(TopoDS_Shape()));
  EXPECT_FALSE(core::computeThickness(BRepPrimAPI_MakeBox(1, 1, 1).Shape())); // no mesh
}

TEST(WallThickness, TheFixtureOncePerDefinition) {
  const core::AssemblyIndex index(test::buildFixtureAssembly());
  for (const core::NodeId part : index.partInstances()) {
    meshed(index.prototypeShape(part), 0.05);
  }
  const core::ThicknessResult result = core::analyseThickness(index, index.roots());
  EXPECT_FALSE(result.cancelled);
  EXPECT_EQ(result.parts.size(), 5U);
  EXPECT_EQ(result.definitions, 2);
  EXPECT_EQ(result.withoutValue, 0);
  EXPECT_NEAR(result.min, 6.0 - 0.0, 0.1) << "the pins are 6 mm across";
  // Every pin shares one map.
  const auto first = result.mapOf(index, index.partInstances()[1]);
  const auto last = result.mapOf(index, index.partInstances().back());
  EXPECT_TRUE(first);
  EXPECT_EQ(first, last);

  core::CancelToken cancel;
  cancel.cancel();
  EXPECT_TRUE(core::analyseThickness(index, index.roots(), {}, {}, &cancel).cancelled);
}
