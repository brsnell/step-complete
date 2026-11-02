// Optional tests on real-world STEP files. Run tests/data/fetch.sh and set
// SC_TEST_DATA_DIR to the download directory; otherwise these tests skip.

#include "core/AssemblyIndex.h"
#include "core/StepImporter.h"

#include <TopExp_Explorer.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>

using namespace sc;

namespace {

std::string modelPath(const char* fileName) {
  const char* dir = std::getenv("SC_TEST_DATA_DIR");
  if (dir == nullptr) {
    return {};
  }
  const auto path = std::filesystem::path(dir) / fileName;
  return std::filesystem::exists(path) ? path.string() : std::string();
}

#define REQUIRE_MODEL(var, file)                                                                             \
  const std::string var = modelPath(file);                                                                   \
  if (var.empty()) {                                                                                         \
    GTEST_SKIP() << file << " not available (run tests/data/fetch.sh and set SC_TEST_DATA_DIR)";             \
  }

int countColoredParts(const core::AssemblyIndex& index) {
  auto colors = XCAFDoc_DocumentTool::ColorTool(index.document()->ocaf()->Main());
  int colored = 0;
  for (const core::NodeId id : index.partInstances()) {
    Quantity_ColorRGBA color;
    const auto& node = index.node(id);
    if (node.inheritedColor || colors->GetColor(node.prototypeLabel, XCAFDoc_ColorSurf, color) ||
        colors->GetColor(node.prototypeLabel, XCAFDoc_ColorGen, color)) {
      ++colored;
    }
  }
  return colored;
}

} // namespace

TEST(RealModels, As1Ap214AssemblyStructureAndColors) {
  REQUIRE_MODEL(path, "as1-oc-214.stp");
  const auto result = core::importStep(path);
  ASSERT_TRUE(result.ok) << result.error;
  const core::AssemblyIndex index(result.document);

  ASSERT_EQ(index.roots().size(), 1u);
  EXPECT_EQ(index.node(index.roots().front()).name, "as1");
  EXPECT_EQ(index.partInstances().size(), 18u);
  EXPECT_EQ(index.uniquePartCount(), 5);
  EXPECT_EQ(result.document->fileUnit(), core::LengthUnit::Millimeter);
  EXPECT_GT(countColoredParts(index), 0) << "AP214 colors must be read";
}

TEST(RealModels, As1Ap203AssemblyStructure) {
  REQUIRE_MODEL(path, "as1_pe_203.stp");
  const auto result = core::importStep(path);
  ASSERT_TRUE(result.ok) << result.error;
  const core::AssemblyIndex index(result.document);
  // Each AP203 product here has two representations: a solid and a wireframe
  // curve set. OCCT models that as a small assembly with both as components,
  // so there are 36 part instances (10 unique), of which 18 are the solids.
  EXPECT_EQ(index.partInstances().size(), 36u);
  EXPECT_EQ(index.uniquePartCount(), 10);
  int solids = 0;
  for (const core::NodeId id : index.partInstances()) {
    solids += TopExp_Explorer(index.prototypeShape(id), TopAbs_FACE).More() ? 1 : 0;
  }
  EXPECT_EQ(solids, 18);
}
