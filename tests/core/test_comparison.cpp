// Model comparison (docs/designs/model-comparison.md): pairing by path, Unchanged / Moved /
// Changed / Added / Removed from exact properties, deviation from the meshes, and the mesh
// distance it uses.

#include "TestModels.h"
#include "core/AssemblyIndex.h"
#include "core/Comparison.h"
#include "core/MeshDistance.h"
#include "core/PartMesh.h"
#include "core/StepImporter.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <TDataStd_Name.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Ax1.hxx>
#include <gp_Trsf.hxx>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace sc;

namespace {

TopLoc_Location at(double x, double y, double z) {
  gp_Trsf trsf;
  trsf.SetTranslation(gp_Vec(x, y, z));
  return TopLoc_Location(trsf);
}

TopoDS_Shape meshed(const TopoDS_Shape& shape) {
  BRepMesh_IncrementalMesh(shape, 0.05);
  return shape;
}

// A revision of the fixture: a plate of the given thickness, two pin pairs (the second shifted
// along X) and one extra cube with the given name and size (none when the name is empty).
core::DocumentPtr revision(double plateThickness, double secondPairShift, const std::string& extra,
                           double extraSize = 5.0) {
  auto document = std::make_shared<core::Document>();
  const OccHandle<XCAFDoc_ShapeTool> shapes = XCAFDoc_DocumentTool::ShapeTool(document->ocaf()->Main());
  const TDF_Label plate =
      shapes->AddShape(meshed(BRepPrimAPI_MakeBox(100.0, 60.0, plateThickness).Shape()), false);
  TDataStd_Name::Set(plate, "Base plate");
  const TDF_Label pin = shapes->AddShape(meshed(BRepPrimAPI_MakeCylinder(3.0, 20.0).Shape()), false);
  TDataStd_Name::Set(pin, "Pin");
  const TDF_Label pair = shapes->NewShape();
  TDataStd_Name::Set(pair, "Pin pair");
  shapes->AddComponent(pair, pin, TopLoc_Location());
  shapes->AddComponent(pair, pin, at(20, 0, 0));
  const TDF_Label root = shapes->NewShape();
  TDataStd_Name::Set(root, "Fixture");
  shapes->AddComponent(root, plate, TopLoc_Location());
  shapes->AddComponent(root, pair, at(10, 10, 10));
  shapes->AddComponent(root, pair, at(50 + secondPairShift, 10, 10));
  if (!extra.empty()) {
    const TDF_Label part =
        shapes->AddShape(meshed(BRepPrimAPI_MakeBox(extraSize, extraSize, extraSize).Shape()), false);
    TDataStd_Name::Set(part, TCollection_ExtendedString(extra.c_str()));
    shapes->AddComponent(root, part, at(0, 70, 0));
  }
  shapes->UpdateAssemblies();
  return document;
}

const core::PartChange* findChange(const core::ComparisonResult& result, const std::string& path,
                                   int nth = 0) {
  for (const core::PartChange& change : result.changes) {
    if (change.path == path && nth-- == 0) {
      return &change;
    }
  }
  return nullptr;
}

} // namespace

TEST(Comparison, IdenticalRevisionsAreUnchanged) {
  // Two imports of one file: the same geometry in different shapes, as two exports would be.
  const std::string path = test::tempPath("revision.stp");
  ASSERT_TRUE(test::writeStep(test::buildFixtureAssembly(false), path));
  const core::ImportResult first = core::importModel(path);
  const core::ImportResult second = core::importModel(path);
  ASSERT_TRUE(first.ok && second.ok);
  const core::AssemblyIndex current(first.document);
  const core::AssemblyIndex other(second.document);

  const auto pairs = core::pairParts(current, other);
  ASSERT_EQ(pairs.size(), 5U);
  for (const auto& [a, b] : pairs) {
    EXPECT_NE(a, core::NoNode);
    EXPECT_NE(b, core::NoNode);
    EXPECT_EQ(core::pairingPath(current, a), core::pairingPath(other, b));
  }
  const core::ComparisonResult result = core::compareModels(current, other);
  EXPECT_EQ(result.unchanged, 5);
  EXPECT_EQ(result.changed + result.moved + result.added + result.removed, 0);
}

TEST(Comparison, FindsChangedMovedAddedAndRemovedParts) {
  const core::AssemblyIndex current(revision(12.0, 5.0, "Bracket")); // thicker plate, moved pair, a bracket
  const core::AssemblyIndex other(revision(10.0, 0.0, "Clip", 4.0)); // a different part, not a renamed one
  const core::ComparisonResult result = core::compareModels(current, other);
  EXPECT_EQ(result.changed, 1);
  EXPECT_EQ(result.unchanged, 2); // the first pin pair
  EXPECT_EQ(result.moved, 2);     // the second pin pair
  EXPECT_EQ(result.added, 1);
  EXPECT_EQ(result.removed, 1);

  const core::PartChange* plate = findChange(result, "Base plate");
  ASSERT_NE(plate, nullptr);
  EXPECT_EQ(plate->kind, core::ChangeKind::Changed);
  EXPECT_NEAR(plate->maxDeviation, 2.0, 1e-6); // the top is 2 mm higher
  ASSERT_FALSE(plate->mesh.IsNull());
  ASSERT_EQ(plate->deviation.size(), static_cast<size_t>(plate->mesh->NbNodes()));
  EXPECT_NEAR(*std::max_element(plate->deviation.begin(), plate->deviation.end()), 2.0, 1e-6)
      << "material added: positive";
  EXPECT_NEAR(result.maxDeviation, 2.0, 1e-6);

  const core::PartChange* firstPin = findChange(result, "Pin pair / Pin", 0);
  const core::PartChange* thirdPin = findChange(result, "Pin pair / Pin", 2);
  ASSERT_NE(firstPin, nullptr);
  ASSERT_NE(thirdPin, nullptr);
  EXPECT_EQ(firstPin->kind, core::ChangeKind::Unchanged);
  EXPECT_EQ(thirdPin->kind, core::ChangeKind::Moved);

  const core::PartChange* bracket = findChange(result, "Bracket");
  ASSERT_NE(bracket, nullptr);
  EXPECT_EQ(bracket->kind, core::ChangeKind::Added);
  EXPECT_EQ(bracket->other, core::NoNode);
  const core::PartChange* clip = findChange(result, "Clip");
  ASSERT_NE(clip, nullptr);
  EXPECT_EQ(clip->kind, core::ChangeKind::Removed);
  EXPECT_EQ(clip->current, core::NoNode);
  EXPECT_EQ(result.changes.back().path, "Clip") << "removed parts come last";
}

TEST(Comparison, AThinnerPlateReadsNegativeAcrossItsTop) {
  // The current plate is 2 mm thinner: its top lies inside the other plate. Its mesh is refined,
  // so points inside the top face read -2 (a box face alone has only its corners, which lie on
  // the other plate's sides at distance 0).
  const core::AssemblyIndex current(revision(10.0, 0.0, ""));
  const core::AssemblyIndex other(revision(12.0, 0.0, ""));
  const core::ComparisonResult result = core::compareModels(current, other);
  const core::PartChange* plate = findChange(result, "Base plate");
  ASSERT_NE(plate, nullptr);
  ASSERT_EQ(plate->kind, core::ChangeKind::Changed);
  int insideTop = 0;
  for (int i = 1; i <= plate->mesh->NbNodes(); ++i) {
    const gp_Pnt p = plate->mesh->Node(i);
    if (std::abs(p.Z() - 10.0) < 1e-9 && p.X() > 5 && p.X() < 95 && p.Y() > 5 && p.Y() < 55) {
      ++insideTop;
      EXPECT_NEAR(plate->deviation[static_cast<size_t>(i - 1)], -2.0, 1e-6);
    }
  }
  EXPECT_GT(insideTop, 10);
}

TEST(Comparison, RenamedPartsPairByShapeThenByName) {
  // "Bracket" was renamed "Clip" with the same geometry in the same place: unchanged, not one
  // added and one removed. "Nut_2" and "NUT" pair by name although the nut grew: changed.
  const core::AssemblyIndex current(revision(10.0, 0.0, "Bracket"));
  const core::AssemblyIndex other(revision(10.0, 0.0, "Clip"));
  const core::ComparisonResult renamed = core::compareModels(current, other);
  EXPECT_EQ(renamed.unchanged, 6);
  EXPECT_EQ(renamed.added + renamed.removed, 0);

  const core::AssemblyIndex withNut(revision(10.0, 0.0, "Nut_2", 5.0));
  const core::AssemblyIndex withBiggerNut(revision(10.0, 0.0, "NUT", 6.0));
  const core::ComparisonResult byName = core::compareModels(withNut, withBiggerNut);
  EXPECT_EQ(byName.changed, 1);
  EXPECT_EQ(byName.added + byName.removed, 0);
  const core::PartChange* nut = findChange(byName, "Nut_2");
  ASSERT_NE(nut, nullptr);
  EXPECT_EQ(nut->kind, core::ChangeKind::Changed);
  EXPECT_NEAR(nut->maxDeviation, std::sqrt(3.0), 1e-6); // the far corner moved by (1, 1, 1)
}

TEST(Comparison, APartTurnedAboutItsCentreIsNotUnchanged) {
  const auto build = [](bool turned) {
    auto document = std::make_shared<core::Document>();
    const OccHandle<XCAFDoc_ShapeTool> shapes = XCAFDoc_DocumentTool::ShapeTool(document->ocaf()->Main());
    TopoDS_Shape block = meshed(BRepPrimAPI_MakeBox(gp_Pnt(-5, -10, -15), 10.0, 20.0, 30.0).Shape());
    if (turned) {
      gp_Trsf turn;
      turn.SetRotation(gp_Ax1(gp_Pnt(), gp_Dir(0, 0, 1)), M_PI / 2.0); // about its own centre
      block = block.Moved(TopLoc_Location(turn));
    }
    TDataStd_Name::Set(shapes->AddShape(block, false), "Block");
    return document;
  };
  const core::AssemblyIndex current(build(true));
  const core::AssemblyIndex other(build(false));
  const core::ComparisonResult result = core::compareModels(current, other);
  ASSERT_EQ(result.changes.size(), 1U);
  EXPECT_NE(result.changes.front().kind, core::ChangeKind::Unchanged); // same centroid, other axes
}

TEST(Comparison, CancelStops) {
  const core::AssemblyIndex current(revision(10.0, 0.0, ""));
  core::CancelToken cancel;
  cancel.cancel();
  EXPECT_TRUE(core::compareModels(current, current, {}, {}, &cancel).cancelled);
}

TEST(MeshDistance, DistancesAndSidesToABox) {
  const TopoDS_Shape box = meshed(BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape());
  gp_Trsf shift;
  shift.SetTranslation(gp_Vec(100.0, 0.0, 0.0));
  const core::MeshDistance distance(core::partMesh(box), shift);
  ASSERT_FALSE(distance.empty());
  const auto above = distance.closest(gp_Pnt(105, 10, 40)); // 10 above the top
  ASSERT_TRUE(above);
  EXPECT_NEAR(above->distance, 10.0, 1e-9);
  EXPECT_NEAR(above->signedDistance, 10.0, 1e-9);
  EXPECT_NEAR(above->point.Z(), 30.0, 1e-9);
  const auto inside = distance.closest(gp_Pnt(103, 10, 15)); // 3 from the face at x = 100
  ASSERT_TRUE(inside);
  EXPECT_NEAR(inside->distance, 3.0, 1e-9);
  EXPECT_NEAR(inside->signedDistance, -3.0, 1e-9);
  EXPECT_FALSE(core::MeshDistance(nullptr).closest(gp_Pnt()));
}
