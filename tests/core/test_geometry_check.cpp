// Measure > Check Geometry (docs/designs/geometry-check.md): invalid geometry, open shells and
// tiny edges on shapes broken on purpose; clean shapes report nothing.

#include "TestModels.h"
#include "core/AssemblyIndex.h"
#include "core/GeometryCheck.h"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Trsf.hxx>

#include <gtest/gtest.h>

#include <algorithm>

using namespace sc;

namespace {

int countKind(const std::vector<core::GeometryIssue>& issues, core::IssueKind kind) {
  return static_cast<int>(std::count_if(
      issues.begin(), issues.end(), [kind](const core::GeometryIssue& issue) { return issue.kind == kind; }));
}

// A box with its top face left out, built into a solid: a solid with a gap.
TopoDS_Shape boxWithoutTop() {
  const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();
  BRep_Builder builder;
  TopoDS_Shell shell;
  builder.MakeShell(shell);
  bool skipped = false;
  for (TopExp_Explorer it(box, TopAbs_FACE); it.More(); it.Next()) {
    if (!skipped) { // the first face, whichever it is
      skipped = true;
      continue;
    }
    builder.Add(shell, it.Current());
  }
  TopoDS_Solid solid;
  builder.MakeSolid(solid);
  builder.Add(solid, shell);
  return solid;
}

} // namespace

TEST(GeometryCheck, ACleanBoxHasNoProblems) {
  int found = -1;
  EXPECT_TRUE(core::checkShape(BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape(), &found).empty());
  EXPECT_EQ(found, 0);
  EXPECT_TRUE(core::checkShape(TopoDS_Shape()).empty());
}

TEST(GeometryCheck, ASolidWithAMissingFaceIsAnOpenShell) {
  const std::vector<core::GeometryIssue> issues = core::checkShape(boxWithoutTop());
  ASSERT_EQ(countKind(issues, core::IssueKind::OpenShell), 1);
  const auto open = std::find_if(issues.begin(), issues.end(), [](const core::GeometryIssue& issue) {
    return issue.kind == core::IssueKind::OpenShell;
  });
  EXPECT_EQ(open->detail, "4 free edges: the solid has a gap");
  int edges = 0;
  for (TopExp_Explorer it(open->shape, TopAbs_EDGE); it.More(); it.Next()) {
    ++edges;
  }
  EXPECT_EQ(edges, 4); // the highlight is the edges around the hole
}

TEST(GeometryCheck, ShortEdgesAreTiny) {
  // 0.0005 mm thick: its four short edges are below TinyEdgeLength.
  const std::vector<core::GeometryIssue> issues =
      core::checkShape(BRepPrimAPI_MakeBox(10.0, 10.0, 0.0005).Shape());
  EXPECT_EQ(countKind(issues, core::IssueKind::TinyEdge), 4);
  for (const core::GeometryIssue& issue : issues) {
    if (issue.kind == core::IssueKind::TinyEdge) {
      EXPECT_EQ(issue.shapeType, "Edge");
      EXPECT_EQ(issue.detail, "0.000500 mm long");
    }
  }
}

TEST(GeometryCheck, ABowTieFaceIsInvalid) {
  // The corners in bow-tie order: the outline crosses itself.
  BRepBuilderAPI_MakePolygon polygon(gp_Pnt(0, 0, 0), gp_Pnt(10, 10, 0), gp_Pnt(10, 0, 0), gp_Pnt(0, 10, 0),
                                     true);
  BRepBuilderAPI_MakeFace face(polygon.Wire(), true);
  ASSERT_TRUE(face.IsDone());
  const std::vector<core::GeometryIssue> issues = core::checkShape(face.Face());
  ASSERT_GE(countKind(issues, core::IssueKind::Invalid), 1);
  const bool selfIntersecting =
      std::any_of(issues.begin(), issues.end(), [](const core::GeometryIssue& issue) {
        return issue.kind == core::IssueKind::Invalid && issue.detail == "Self-intersecting wire";
      });
  EXPECT_TRUE(selfIntersecting);
}

TEST(GeometryCheck, RowsPerPartAreCappedButAllAreCounted) {
  // 60 thin boxes: 240 tiny edges in one shape.
  BRep_Builder builder;
  TopoDS_Compound compound;
  builder.MakeCompound(compound);
  for (int i = 0; i < 60; ++i) {
    gp_Trsf move;
    move.SetTranslation(gp_Vec(20.0 * i, 0, 0));
    builder.Add(compound, BRepPrimAPI_MakeBox(10.0, 10.0, 0.0005).Shape().Moved(TopLoc_Location(move)));
  }
  int found = 0;
  const std::vector<core::GeometryIssue> issues = core::checkShape(compound, &found);
  EXPECT_EQ(static_cast<int>(issues.size()), core::MaxIssuesPerPart);
  EXPECT_EQ(found, 240);
}

TEST(GeometryCheck, TheFixtureChecksCleanOncePerPart) {
  const core::AssemblyIndex index(test::buildFixtureAssembly());
  const core::GeometryCheckResult result = core::checkGeometry(index, index.roots());
  EXPECT_FALSE(result.cancelled);
  EXPECT_EQ(result.partsChecked, 2); // 5 instances of 2 parts
  EXPECT_EQ(result.partsWithIssues, 0);
  EXPECT_EQ(result.issueCount, 0);
  EXPECT_TRUE(result.issues.empty());

  core::CancelToken cancel;
  cancel.cancel();
  EXPECT_TRUE(core::checkGeometry(index, index.roots(), {}, &cancel).cancelled);
}

TEST(GeometryCheck, StatusesInPlainWords) {
  EXPECT_EQ(core::statusText(BRepCheck_SelfIntersectingWire), "Self-intersecting wire");
  EXPECT_EQ(core::statusText(BRepCheck_FreeEdge), "Free edge");
  EXPECT_EQ(core::statusText(BRepCheck_NotClosed), "Not closed");
  EXPECT_STREQ(core::toString(core::IssueKind::OpenShell), "Open shell");
}
