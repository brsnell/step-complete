#include "TestModels.h"

#include "core/AssemblyIndex.h"
#include "core/StepImporter.h"

#include <BRepBndLib.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Quantity_Color.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iterator>

using namespace sc;
using core::AssemblyIndex;
using core::NodeId;
using core::NodeKind;

namespace {

const core::AssemblyNode* findByName(const AssemblyIndex& index, const std::string& name,
                                     int occurrence = 0) {
  for (const auto& node : index.nodes()) {
    if (node.name == name && occurrence-- == 0) {
      return &node;
    }
  }
  return nullptr;
}

gp_XYZ translationOf(const core::AssemblyNode& node) {
  return node.location.Transformation().TranslationPart();
}

core::ImportResult importFixture() {
  const std::string path = test::tempPath("fixture.step");
  EXPECT_TRUE(test::writeStep(test::buildFixtureAssembly(false), path));
  return core::importStep(path);
}

// Replaces every millimetre length unit with a STEP conversion-based inch unit.
bool relabelLengthUnitAsInch(const std::string& path) {
  std::ifstream in(path);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const std::string mm = "( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) )";
  const std::string inch = "( CONVERSION_BASED_UNIT('INCH',#990001) LENGTH_UNIT() NAMED_UNIT(#990002) )";
  int replaced = 0;
  for (size_t pos = text.find(mm); pos != std::string::npos; pos = text.find(mm, pos + inch.size())) {
    text.replace(pos, mm.size(), inch);
    ++replaced;
  }
  const size_t dataEnd = text.rfind("ENDSEC;");
  if (replaced == 0 || dataEnd == std::string::npos) {
    return false;
  }
  text.insert(dataEnd, "#990001 = LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#990003);\n"
                       "#990002 = DIMENSIONAL_EXPONENTS(1.,0.,0.,0.,0.,0.,0.);\n"
                       "#990003 = ( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) );\n");
  std::ofstream(path, std::ios::trunc) << text;
  return true;
}

} // namespace

// --- Assembly index on an in-memory document (no file I/O) ---------------------

TEST(AssemblyIndex, FlattensInstancesAndAccumulatesPlacements) {
  const AssemblyIndex index(test::buildFixtureAssembly());

  ASSERT_EQ(index.roots().size(), 1u);
  EXPECT_EQ(index.nodes().size(), 8u);
  EXPECT_EQ(index.partInstances().size(), 5u);
  EXPECT_EQ(index.uniquePartCount(), 2);

  const auto& root = index.node(index.roots().front());
  EXPECT_EQ(root.name, "Fixture");
  EXPECT_EQ(root.kind, NodeKind::Assembly);
  EXPECT_EQ(root.children.size(), 3u);

  // Second pin of the second pair: (50,10,10) + (20,0,0).
  const auto* pin = findByName(index,
                               "Pin \xC3\x98"
                               "6",
                               3);
  ASSERT_NE(pin, nullptr);
  EXPECT_EQ(pin->depth, 2);
  EXPECT_TRUE(translationOf(*pin).IsEqual(gp_XYZ(70, 10, 10), 1e-9));
  EXPECT_EQ(index.instanceCount(pin->id), 4);
  EXPECT_EQ(index.displayPath(pin->id), "Fixture / Pin pair / Pin \xC3\x98"
                                        "6");
}

TEST(AssemblyIndex, InheritsInstanceColorsAndVisibility) {
  const AssemblyIndex index(test::buildFixtureAssembly());
  const auto* firstPin = findByName(index,
                                    "Pin \xC3\x98"
                                    "6",
                                    0);
  const auto* secondPin = findByName(index,
                                     "Pin \xC3\x98"
                                     "6",
                                     1);
  ASSERT_TRUE(firstPin && secondPin);
  EXPECT_FALSE(firstPin->inheritedColor.has_value());
  ASSERT_TRUE(secondPin->inheritedColor.has_value());
  EXPECT_TRUE(secondPin->inheritedColor->GetRGB().IsEqual(Quantity_Color(Quantity_NOC_GREEN)));

  const auto* visiblePair = findByName(index, "Pin pair", 0);
  const auto* hiddenPair = findByName(index, "Pin pair", 1);
  ASSERT_TRUE(visiblePair && hiddenPair);
  EXPECT_TRUE(visiblePair->visibleInFile);
  EXPECT_FALSE(hiddenPair->visibleInFile);
  for (const NodeId child : hiddenPair->children) {
    EXPECT_FALSE(index.node(child).visibleInFile) << "visibility must propagate to children";
  }
}

TEST(AssemblyIndex, SubtreeQueries) {
  const AssemblyIndex index(test::buildFixtureAssembly());
  const NodeId root = index.roots().front();
  EXPECT_EQ(index.subtree(root).size(), 8u);
  EXPECT_EQ(index.partsBelow(root).size(), 5u);
  const auto* pair = findByName(index, "Pin pair");
  ASSERT_NE(pair, nullptr);
  EXPECT_EQ(index.partsBelow(pair->id).size(), 2u);

  // World shape carries the accumulated placement.
  const auto* pin = findByName(index,
                               "Pin \xC3\x98"
                               "6",
                               1); // (10,10,10) + (20,0,0)
  Bnd_Box box;
  BRepBndLib::Add(index.worldShape(pin->id), box);
  double xmin, ymin, zmin, xmax, ymax, zmax;
  box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
  EXPECT_NEAR((xmin + xmax) / 2.0, 30.0, 1e-6);
  EXPECT_NEAR(zmin, 10.0, 1e-6);
}

TEST(AssemblyIndex, ReadsUserDefinedAttributes) {
  const AssemblyIndex index(test::buildFixtureAssembly());
  const auto* plate = findByName(index, "Base plate");
  ASSERT_NE(plate, nullptr);
  const auto attributes = index.attributes(plate->id);
  using Entry = std::pair<std::string, std::string>;
  // Sorted by key across value types.
  const std::vector<Entry> expected{{"Mass (kg)", "0.162000"},
                                    {"Material", "Aluminium 6061"},
                                    {"Part number", "BP-100"},
                                    {"Revision", "3"}};
  EXPECT_EQ(attributes, expected);

  const auto* pin = findByName(index, "Pin \xC3\x98"
                                      "6");
  ASSERT_NE(pin, nullptr);
  EXPECT_TRUE(index.attributes(pin->id).empty());
}

TEST(AssemblyIndex, EmptyForNullDocument) {
  const AssemblyIndex index(nullptr);
  EXPECT_TRUE(index.isEmpty());
}

// --- STEP import round trips ------------------------------------------------------

TEST(StepImport, RoundTripsStructureNamesAndColors) {
  const core::ImportResult result = importFixture();
  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_TRUE(result.document);
  EXPECT_EQ(result.stats.uniqueParts, 2);

  const AssemblyIndex index(result.document);
  EXPECT_EQ(index.nodes().size(), 8u);
  EXPECT_EQ(index.partInstances().size(), 5u);
  EXPECT_EQ(index.uniquePartCount(), 2);
  EXPECT_EQ(index.node(index.roots().front()).name, "Fixture");

  const auto* plate = findByName(index, "Base plate");
  ASSERT_NE(plate, nullptr);
  auto colors = XCAFDoc_DocumentTool::ColorTool(result.document->ocaf()->Main());
  Quantity_Color color;
  ASSERT_TRUE(colors->GetColor(plate->prototypeLabel, XCAFDoc_ColorSurf, color));
  EXPECT_TRUE(color.IsEqual(Quantity_Color(Quantity_NOC_RED)));

  const auto* pin = findByName(index,
                               "Pin \xC3\x98"
                               "6",
                               3);
  ASSERT_NE(pin, nullptr) << "non-ASCII product name must survive the round trip";
  EXPECT_TRUE(translationOf(*pin).IsEqual(gp_XYZ(70, 10, 10), 1e-6));
}

TEST(StepImport, ReportsFileUnitAndConvertsGeometryToMillimeters) {
  // OCCT's writer handles units inconsistently across versions, so write in mm
  // and relabel the file's length unit as inch: the plate is then 100 in wide.
  const std::string path = test::tempPath("fixture-inch.step");
  ASSERT_TRUE(test::writeStep(test::buildFixtureAssembly(false), path));
  ASSERT_TRUE(relabelLengthUnitAsInch(path));
  const core::ImportResult result = core::importStep(path);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.document->fileUnit(), core::LengthUnit::Inch) << result.document->fileUnitName();

  const AssemblyIndex index(result.document);
  const auto* plate = findByName(index, "Base plate");
  ASSERT_NE(plate, nullptr);
  Bnd_Box box;
  BRepBndLib::Add(index.worldShape(plate->id), box);
  double xmin, ymin, zmin, xmax, ymax, zmax;
  box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
  EXPECT_NEAR(xmax - xmin, 2540.0, 1e-3) << "geometry must be in mm regardless of file unit";
}

TEST(StepImport, ReportsProgressUpToCompletion) {
  const std::string path = test::tempPath("progress.step");
  ASSERT_TRUE(test::writeStep(test::buildFixtureAssembly(false), path));
  std::vector<double> fractions;
  const auto result =
      core::importStep(path, {}, [&fractions](double f, std::string_view) { fractions.push_back(f); });
  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_FALSE(fractions.empty());
  EXPECT_DOUBLE_EQ(fractions.back(), 1.0);
  EXPECT_TRUE(std::is_sorted(fractions.begin(), fractions.end()));
}

TEST(StepImport, HonorsCancellation) {
  const std::string path = test::tempPath("cancel.step");
  ASSERT_TRUE(test::writeStep(test::buildFixtureAssembly(false), path));
  core::CancelToken cancel;
  cancel.cancel();
  const auto result = core::importStep(path, {}, {}, &cancel);
  EXPECT_FALSE(result.ok);
  EXPECT_TRUE(result.cancelled);
  EXPECT_FALSE(result.document);
}

TEST(StepImport, FailsCleanlyOnMissingOrInvalidFiles) {
  const auto missing = core::importStep(test::tempPath("does-not-exist.step"));
  EXPECT_FALSE(missing.ok);
  EXPECT_FALSE(missing.cancelled);
  EXPECT_FALSE(missing.error.empty());

  const std::string garbagePath = test::tempPath("garbage.step");
  std::ofstream(garbagePath) << "this is not a STEP file\n";
  const auto garbage = core::importStep(garbagePath);
  EXPECT_FALSE(garbage.ok);
  EXPECT_FALSE(garbage.error.empty());
}

TEST(StepImport, PremeshesPartsForTheViewer) {
  const auto result = importFixture();
  ASSERT_TRUE(result.ok) << result.error;
  const AssemblyIndex index(result.document);
  for (const NodeId part : index.partInstances()) {
    TopLoc_Location location;
    bool hasTriangulation = false;
    for (TopExp_Explorer face(index.prototypeShape(part), TopAbs_FACE); face.More(); face.Next()) {
      hasTriangulation = !BRep_Tool::Triangulation(TopoDS::Face(face.Current()), location).IsNull();
      if (!hasTriangulation) {
        break;
      }
    }
    EXPECT_TRUE(hasTriangulation) << index.node(part).name;
  }
}
