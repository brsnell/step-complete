// PmiModel: extraction of GD&T and saved views from an XCAF document.
//
// The synthetic fixture builds PMI the way OCCT's STEP reader stores it
// (including one datum label per tolerance reference), so exact expectations
// hold on every OCCT version. The NIST tests check real AP242 files when
// tests/data/fetch.sh has been run (SC_TEST_DATA_DIR); they print a summary so
// CI logs document what each OCCT version extracts.

#include "PmiFixture.h"
#include "core/Document.h"
#include "core/OccCompat.h"
#include "core/PmiModel.h"
#include "core/StepImporter.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TDataStd_Name.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFDoc_ClippingPlaneTool.hxx>
#include <XCAFDoc_Datum.hxx>
#include <XCAFDoc_DimTolTool.hxx>
#include <XCAFDoc_Dimension.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_GeomTolerance.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_View.hxx>
#include <XCAFDoc_ViewTool.hxx>
#include <XCAFView_Object.hxx>

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>

using namespace sc;
using core::PmiKind;
using core::PmiModel;

namespace {

const core::PmiItem* byLabel(const PmiModel& model, const TDF_Label& label) {
  const core::PmiId id = model.itemFor(label);
  return id == core::kNoPmi ? nullptr : &model.item(id);
}

} // namespace

TEST(PmiModel, EmptyForNullOrPlainDocuments) {
  EXPECT_TRUE(PmiModel(nullptr).isEmpty());
  EXPECT_TRUE(PmiModel(std::make_shared<core::Document>()).isEmpty());
}

TEST(PmiModel, ReadsDimensionsTolerancesAndDatums) {
  const test::PmiFixture f = test::buildPmiFixture();
  const PmiModel model(f.document);

  EXPECT_EQ(model.count(PmiKind::Dimension), 2);
  EXPECT_EQ(model.count(PmiKind::GeometricTolerance), 2);
  EXPECT_EQ(model.count(PmiKind::Datum), 1) << "per-reference datum labels merge into one datum";
  EXPECT_EQ(model.count(PmiKind::DatumTarget), 0);
  EXPECT_EQ(model.uninterpretedCount(), 1);
  for (size_t i = 0; i < model.items().size(); ++i) {
    EXPECT_EQ(model.items()[i].id, static_cast<core::PmiId>(i));
  }

  const core::PmiItem* datum = byLabel(model, f.datumA);
  ASSERT_NE(datum, nullptr);
  EXPECT_EQ(datum->text, "A");
  EXPECT_EQ(datum->name, "Datum A");
  EXPECT_EQ(model.itemFor(f.datumARef), datum->id) << "the reference copy maps to the same datum";
  ASSERT_EQ(datum->targetLabels.size(), 1u);
  EXPECT_TRUE(datum->targetLabels.front() == f.bottom);
  EXPECT_TRUE(datum->ownerPart == f.part);

  const core::PmiItem* flatness = byLabel(model, f.flatness);
  ASSERT_NE(flatness, nullptr);
  EXPECT_EQ(flatness->kind, PmiKind::GeometricTolerance);
  EXPECT_TRUE(flatness->semantic);
  EXPECT_TRUE(flatness->datums.empty());
  EXPECT_FALSE(flatness->frame.empty());
  ASSERT_EQ(flatness->targets.size(), 1u);
  EXPECT_EQ(flatness->targets.front().ShapeType(), TopAbs_FACE);
  EXPECT_TRUE(flatness->ownerPart == f.part);

  const core::PmiItem* parallelism = byLabel(model, f.parallelism);
  ASSERT_NE(parallelism, nullptr);
  ASSERT_EQ(parallelism->datums.size(), 1u);
  EXPECT_EQ(parallelism->datums.front(), datum->id);
  ASSERT_GE(parallelism->frame.size(), 3u) << "symbol, zone and datum cells";
  EXPECT_EQ(parallelism->frame.back(), "A");

  const core::PmiItem* height = byLabel(model, f.height);
  ASSERT_NE(height, nullptr);
  EXPECT_EQ(height->name, "Height") << "the presentation name from the file is the display name";
  EXPECT_EQ(model.itemFor(f.heightCopy), height->id)
      << "a duplicate presentation folds into its semantic item";
  EXPECT_FALSE(height->presentation.IsNull());
  ASSERT_TRUE(height->textPosition && height->plane && height->point1 && height->point2);
  EXPECT_TRUE(height->textPosition->IsEqual(gp_Pnt(110, 0, 10), 1e-9));
  EXPECT_TRUE(height->point2->IsEqual(gp_Pnt(100, 0, 20), 1e-9));
  ASSERT_EQ(height->targets.size(), 1u);
  ASSERT_EQ(height->secondTargets.size(), 1u);
  EXPECT_TRUE(height->targetLabels.front() == f.top);
  EXPECT_TRUE(height->secondTargetLabels.front() == f.bottom);
  EXPECT_NE(height->text.find("20"), std::string::npos) << height->text;

  const core::PmiItem* note = byLabel(model, f.label);
  ASSERT_NE(note, nullptr);
  EXPECT_FALSE(note->semantic) << "graphical-only annotations are present but not interpreted";
  EXPECT_EQ(note->typeName, "Annotation");
}

TEST(PmiModel, FindsPmiForAShapeOrItsSubShapes) {
  const test::PmiFixture f = test::buildPmiFixture();
  const PmiModel model(f.document);
  // The top face: flatness, parallelism and the height dimension.
  EXPECT_EQ(model.itemsTargeting(f.top).size(), 3u);
  EXPECT_EQ(model.itemsTargeting(f.bottom).size(), 2u); // datum A, height
  // The part label covers all its sub-shapes.
  EXPECT_EQ(model.itemsTargeting(f.part).size(), model.items().size());
}

TEST(PmiModel, ReadsSavedViews) {
  const test::PmiFixture f = test::buildPmiFixture();
  const PmiModel model(f.document);
  ASSERT_EQ(model.views().size(), 1u);
  const core::PmiSavedView& view = model.views().front();
  EXPECT_EQ(view.name, "Front");
  EXPECT_FALSE(view.perspective);
  EXPECT_TRUE(view.projectionPoint.IsEqual(gp_Pnt(50, -200, 10), 1e-9));
  EXPECT_TRUE(view.viewDirection.IsEqual(gp_Dir(0, 1, 0), 1e-9));
  EXPECT_TRUE(view.upDirection.IsEqual(gp_Dir(0, 0, 1), 1e-9));
  EXPECT_DOUBLE_EQ(view.zoom, 1.5);
  EXPECT_DOUBLE_EQ(view.windowWidth, 120.0);
  ASSERT_TRUE(view.frontPlaneDistance.has_value());
  EXPECT_DOUBLE_EQ(*view.frontPlaneDistance, 5.0);
  EXPECT_FALSE(view.backPlaneDistance.has_value());
  ASSERT_EQ(view.clippingPlanes.size(), 1u);
  EXPECT_TRUE(view.clippingPlanes.front().Axis().Direction().IsEqual(gp_Dir(1, 0, 0), 1e-9));
  ASSERT_EQ(view.shapes.size(), 1u);
  EXPECT_TRUE(view.shapes.front() == f.part);
  ASSERT_EQ(view.pmi.size(), 2u);
  EXPECT_EQ(view.pmi[0], model.itemFor(f.flatness));
  EXPECT_EQ(view.pmi[1], model.itemFor(f.height));
}

// --- NIST MBE PMI models (optional) ----------------------------------------------

namespace {

std::string nistPath(const char* fileName) {
  const char* dir = std::getenv("SC_TEST_DATA_DIR");
  if (dir == nullptr) {
    return {};
  }
  const auto path = std::filesystem::path(dir) / fileName;
  return std::filesystem::exists(path) ? path.string() : std::string();
}

struct NistSummary {
  int dimensions, tolerances, datums, datumTargets, uninterpreted, views, presentations, targeted, items;
};

NistSummary summarize(const char* file, const PmiModel& model) {
  NistSummary s{model.count(PmiKind::Dimension),
                model.count(PmiKind::GeometricTolerance),
                model.count(PmiKind::Datum),
                model.count(PmiKind::DatumTarget),
                model.uninterpretedCount(),
                static_cast<int>(model.views().size()),
                0,
                0,
                static_cast<int>(model.items().size())};
  for (const core::PmiItem& item : model.items()) {
    s.presentations += item.presentation.IsNull() ? 0 : 1;
    s.targeted += item.targets.empty() ? 0 : 1;
  }
  // Documents what this OCCT version extracts (see docs/pmi-coverage.md).
  std::printf("[PMI] %s OCCT %s: dimensions=%d tolerances=%d datums=%d targets=%d uninterpreted=%d "
              "views=%d presentations=%d targeted=%d/%d\n",
              file, OCC_VERSION_COMPLETE, s.dimensions, s.tolerances, s.datums, s.datumTargets,
              s.uninterpreted, s.views, s.presentations, s.targeted, s.items);
  return s;
}

} // namespace

class NistPmi : public ::testing::TestWithParam<const char*> {};

TEST_P(NistPmi, ExtractsAnnotations) {
  const std::string path = nistPath(GetParam());
  if (path.empty()) {
    GTEST_SKIP() << GetParam() << " not available (run tests/data/fetch.sh and set SC_TEST_DATA_DIR)";
  }
  const core::ImportResult result = core::importStep(path);
  ASSERT_TRUE(result.ok) << result.error;
  const PmiModel model(result.document);
  const NistSummary s = summarize(GetParam(), model);

  // Every NIST AP242 test model has dimensions, tolerances and datums.
  EXPECT_GT(s.dimensions, 0);
  EXPECT_GT(s.tolerances, 0);
  EXPECT_GT(s.datums + s.datumTargets, 0);
  // Most annotations must resolve to the geometry they apply to.
  EXPECT_GT(s.targeted * 2, s.items);
  for (const core::PmiItem& item : model.items()) {
    EXPECT_FALSE(item.name.empty());
    if (item.semantic) {
      EXPECT_FALSE(item.text.empty()) << item.name;
    }
    for (const core::PmiId datum : item.datums) {
      EXPECT_TRUE(model.item(datum).kind == PmiKind::Datum || model.item(datum).kind == PmiKind::DatumTarget);
    }
  }
  for (const core::PmiSavedView& view : model.views()) {
    EXPECT_FALSE(view.name.empty());
  }
}

INSTANTIATE_TEST_SUITE_P(Models, NistPmi,
                         ::testing::Values("nist_ctc_01_asme1_ap242-e1.stp", "nist_ctc_02_asme1_ap242-e2.stp",
                                           "nist_ctc_03_asme1_ap242-e2.stp", "nist_ctc_04_asme1_ap242-e1.stp",
                                           "nist_ctc_05_asme1_ap242-e1.stp", "nist_ftc_06_asme1_ap242-e2.stp",
                                           "nist_ftc_07_asme1_ap242-e2.stp",
                                           "nist_ftc_10_asme1_ap242-e2.stp"),
                         [](const auto& info) {
                           std::string name = info.param;
                           name = name.substr(0, name.find('_', 9)); // "nist_ctc_01"
                           for (char& c : name) {
                             c = c == '_' ? 'x' : c;
                           }
                           return name;
                         });

TEST(NistPmi, Ctc01SavedViewShowsItsAnnotations) {
  const std::string path = nistPath("nist_ctc_01_asme1_ap242-e1.stp");
  if (path.empty()) {
    GTEST_SKIP() << "nist_ctc_01 not available";
  }
  const PmiModel model(core::importStep(path).document);
  ASSERT_EQ(model.views().size(), 1u);
  EXPECT_EQ(model.views().front().name, "MBD_0");
  EXPECT_GE(model.views().front().pmi.size(), 15u);
  // CTC-01 uses datums A, B and C.
  std::vector<std::string> letters;
  for (const core::PmiItem& item : model.items()) {
    if (item.kind == PmiKind::Datum) {
      letters.push_back(item.text);
    }
  }
  EXPECT_EQ(letters, (std::vector<std::string>{"A", "B", "C"}));
}
