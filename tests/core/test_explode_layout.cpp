// View > Exploded View (docs/designs/exploded-view.md): the offsets by assembly level.

#include "TestModels.h"
#include "core/AssemblyIndex.h"
#include "core/ExplodeLayout.h"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <TDataStd_Name.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

#include <gtest/gtest.h>

#include <string>

using namespace sc;

namespace {

TopLoc_Location at(double x) {
  gp_Trsf trsf;
  trsf.SetTranslation(gp_Vec(x, 0.0, 0.0));
  return TopLoc_Location(trsf);
}

// 10 mm cubes along x.
struct Builder {
  core::DocumentPtr document = std::make_shared<core::Document>();
  OccHandle<XCAFDoc_ShapeTool> shapes = XCAFDoc_DocumentTool::ShapeTool(document->ocaf()->Main());
  TDF_Label cube = shapes->AddShape(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), false);

  TDF_Label assembly(const char* name) {
    const TDF_Label label = shapes->NewShape();
    TDataStd_Name::Set(label, name);
    return label;
  }
  core::AssemblyIndex index() {
    shapes->UpdateAssemblies();
    return core::AssemblyIndex(document);
  }
};

core::NodeId find(const core::AssemblyIndex& index, const std::string& path) {
  for (const core::AssemblyNode& node : index.nodes()) {
    if (index.displayPath(node.id) == path) {
      return node.id;
    }
  }
  return core::NoNode;
}

constexpr double Tolerance = 1e-3; // bounding boxes carry the shapes' tolerance

} // namespace

TEST(ExplodeLayout, AFlatAssemblyExplodesRadially) {
  Builder builder;
  const TDF_Label root = builder.assembly("Root");
  builder.shapes->AddComponent(root, builder.cube, at(0));  // center x = 5
  builder.shapes->AddComponent(root, builder.cube, at(20)); // 25: the assembly's center
  builder.shapes->AddComponent(root, builder.cube, at(40)); // 45
  const core::AssemblyIndex index = builder.index();
  const core::ExplodeLayout layout(index);
  ASSERT_TRUE(layout.canExplode());

  const std::vector<core::NodeId>& parts = index.partInstances();
  ASSERT_EQ(parts.size(), 3U);
  EXPECT_NEAR(layout.offset(parts[0], 1.0).X(), -20.0, Tolerance);
  EXPECT_NEAR(layout.offset(parts[2], 1.0).X(), 20.0, Tolerance);
  EXPECT_NEAR(layout.offset(parts[2], 0.5).X(), 10.0, Tolerance); // linear in the factor
  EXPECT_NEAR(layout.offset(parts[0], 1.0).Y(), 0.0, Tolerance);
  EXPECT_NEAR(layout.offset(parts[0], 1.0).Z(), 0.0, Tolerance);
  // The part on the center stays, and so does the assembly.
  EXPECT_NEAR(layout.offset(parts[1], 1.0).Magnitude(), 0.0, Tolerance);
  EXPECT_NEAR(layout.offset(index.roots().front(), 1.0).Magnitude(), 0.0, Tolerance);
}

TEST(ExplodeLayout, ASubAssemblyMovesAsAUnitAndSpreadsAroundItsCenter) {
  Builder builder;
  const TDF_Label pair = builder.assembly("Pair");
  builder.shapes->AddComponent(pair, builder.cube, at(0));
  builder.shapes->AddComponent(pair, builder.cube, at(20));
  const TDF_Label root = builder.assembly("Root");
  builder.shapes->AddComponent(root, builder.cube, at(0)); // center 5
  builder.shapes->AddComponent(root, pair, at(100));       // cubes at 105 and 125, center 115
  const core::AssemblyIndex index = builder.index();
  const core::ExplodeLayout layout(index);

  // The model spans 0..130 (center 65).
  const std::vector<core::NodeId>& parts = index.partInstances();
  ASSERT_EQ(parts.size(), 3U);
  const core::NodeId pairNode = index.node(parts[1]).parent;
  ASSERT_EQ(pairNode, index.node(parts[2]).parent);
  EXPECT_NEAR(layout.offset(parts[0], 1.0).X(), 5.0 - 65.0, Tolerance);
  EXPECT_NEAR(layout.offset(pairNode, 1.0).X(), 115.0 - 65.0, Tolerance);
  // Each pin: the pair's move plus its own away from the pair's center.
  EXPECT_NEAR(layout.offset(parts[1], 1.0).X(), 50.0 - 10.0, Tolerance);
  EXPECT_NEAR(layout.offset(parts[2], 1.0).X(), 50.0 + 10.0, Tolerance);
}

TEST(ExplodeLayout, FactorZeroIsTheAssembledModel) {
  const core::AssemblyIndex index(test::buildFixtureAssembly());
  const core::ExplodeLayout layout(index);
  ASSERT_TRUE(layout.canExplode());
  const std::vector<gp_Vec> offsets = layout.offsets(0.0);
  ASSERT_EQ(offsets.size(), index.nodes().size());
  for (const gp_Vec& offset : offsets) {
    EXPECT_EQ(offset.Magnitude(), 0.0);
  }
  // The two pins of a pair move apart from each other.
  const core::NodeId pin = find(index, "Fixture / Pin pair / Pin \xC3\x98"
                                       "6");
  ASSERT_NE(pin, core::NoNode);
  EXPECT_GT(layout.offsets(1.0)[static_cast<size_t>(pin)].Magnitude(), 1.0);
}

TEST(ExplodeLayout, NothingToExplode) {
  EXPECT_FALSE(core::ExplodeLayout().canExplode());
  EXPECT_EQ(core::ExplodeLayout().offset(0, 1.0).Magnitude(), 0.0);

  Builder single;
  const TDF_Label root = single.assembly("Root");
  single.shapes->AddComponent(root, single.cube, at(0));
  EXPECT_FALSE(core::ExplodeLayout(single.index()).canExplode());

  Builder sameSpot; // two parts with the same center
  const TDF_Label other = sameSpot.assembly("Root");
  sameSpot.shapes->AddComponent(other, sameSpot.cube, at(0));
  sameSpot.shapes->AddComponent(other, sameSpot.cube, at(0));
  EXPECT_FALSE(core::ExplodeLayout(sameSpot.index()).canExplode());
}

TEST(ExplodeLayout, AnUnboundedPartDoesNotThrowTheOthers) {
  // An untrimmed plane has an open bounding box (infinite extent): it must neither move nor move
  // the others, which keep the layout of the bounded parts.
  Builder builder;
  const TDF_Label plane = builder.shapes->AddShape(
      BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1))).Shape(), false);
  const TDF_Label root = builder.assembly("Root");
  builder.shapes->AddComponent(root, builder.cube, at(0));  // center 5
  builder.shapes->AddComponent(root, builder.cube, at(40)); // center 45
  builder.shapes->AddComponent(root, plane, at(0));
  const core::AssemblyIndex index = builder.index();
  const core::ExplodeLayout layout(index);
  ASSERT_TRUE(layout.canExplode());
  const std::vector<core::NodeId>& parts = index.partInstances();
  ASSERT_EQ(parts.size(), 3U);
  EXPECT_NEAR(layout.offset(parts[0], 1.0).X(), -20.0, Tolerance);
  EXPECT_NEAR(layout.offset(parts[1], 1.0).X(), 20.0, Tolerance);
  EXPECT_EQ(layout.offset(parts[2], 1.0).Magnitude(), 0.0);
}
