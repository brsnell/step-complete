// IGES import (docs/designs/iges-import.md): names, colors, units, detection by content, gzip,
// the open-file cache, and the format helpers.

#include "TestModels.h"
#include "core/AssemblyIndex.h"
#include "core/FileFormat.h"
#include "core/StepImporter.h"

#include <BRepBndLib.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <Bnd_Box.hxx>
#include <TDataStd_Name.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Trsf.hxx>

#include <gtest/gtest.h>
#include <zlib.h>

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace sc;

namespace {

// Two named, colored solids: what a CAD system's IGES export looks like (no assembly).
core::DocumentPtr blockAndPin() {
  auto document = std::make_shared<core::Document>();
  OccHandle<XCAFDoc_ShapeTool> shapes = XCAFDoc_DocumentTool::ShapeTool(document->ocaf()->Main());
  OccHandle<XCAFDoc_ColorTool> colors = XCAFDoc_DocumentTool::ColorTool(document->ocaf()->Main());
  const TDF_Label block = shapes->AddShape(BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape(), false);
  TDataStd_Name::Set(block, "Block");
  colors->SetColor(block, Quantity_Color(Quantity_NOC_RED), XCAFDoc_ColorSurf);
  gp_Trsf move;
  move.SetTranslation(gp_Vec(50.0, 0.0, 0.0));
  const TDF_Label pin =
      shapes->AddShape(BRepPrimAPI_MakeCylinder(3.0, 20.0).Shape().Moved(TopLoc_Location(move)), false);
  TDataStd_Name::Set(pin, "Pin");
  colors->SetColor(pin, Quantity_Color(Quantity_NOC_GREEN), XCAFDoc_ColorSurf);
  return document;
}

Bnd_Box worldBox(const core::AssemblyIndex& index, core::NodeId part) {
  Bnd_Box box;
  BRepBndLib::Add(index.worldShape(part), box, false);
  return box;
}

std::string readAll(const std::string& path) {
  std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeAll(const std::string& path, const std::string& bytes) {
  std::ofstream(std::filesystem::u8path(path), std::ios::binary) << bytes;
}

} // namespace

TEST(IgesImport, ReadsNamedColoredParts) {
  const std::string path = test::tempPath("block_and_pin.igs");
  ASSERT_TRUE(test::writeIges(blockAndPin(), path));
  EXPECT_EQ(core::detectFileFormat(path), core::FileFormat::Iges);

  const core::ImportResult result = core::importModel(path);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.document->format(), core::FileFormat::Iges);
  EXPECT_EQ(result.document->fileUnit(), core::LengthUnit::Millimeter);
  EXPECT_EQ(result.document->fileUnitName(), "MM");
  EXPECT_EQ(result.stats.uniqueParts, 2);

  const core::AssemblyIndex index(result.document);
  ASSERT_EQ(index.partInstances().size(), 2U);
  const core::AssemblyNode& block = index.node(index.partInstances()[0]);
  const core::AssemblyNode& pin = index.node(index.partInstances()[1]);
  EXPECT_EQ(block.name, "Block");
  EXPECT_EQ(pin.name, "Pin");
  ASSERT_TRUE(block.inheritedColor);
  EXPECT_GT(block.inheritedColor->GetRGB().Red(), 0.9);
  ASSERT_TRUE(pin.inheritedColor);
  EXPECT_GT(pin.inheritedColor->GetRGB().Green(), 0.9);

  const Bnd_Box pinBox = worldBox(index, pin.id);
  EXPECT_NEAR(pinBox.CornerMin().X(), 47.0, 1e-3);
  EXPECT_NEAR(pinBox.CornerMax().Z(), 20.0, 1e-3);
}

TEST(IgesImport, AnInchFileIsScaledToMillimetres) {
  const std::string path = test::tempPath("inch.iges");
  ASSERT_TRUE(test::writeIges(blockAndPin(), path, "INCH"));
  const core::ImportResult result = core::importModel(path);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.document->fileUnit(), core::LengthUnit::Inch);
  // The geometry round-trips through inches and comes back in millimetres.
  const core::AssemblyIndex index(result.document);
  const Bnd_Box block = worldBox(index, index.partInstances()[0]);
  EXPECT_NEAR(block.CornerMax().Z() - block.CornerMin().Z(), 30.0, 1e-3);
}

TEST(IgesImport, TheContentDecidesTheFormat) {
  // STEP saved as .igs, and IGES saved as .stp, open as what they are. (No hidden instance: OCCT
  // 7.6's STEP writer crashes in WriteColors on it, as the other STEP-writing tests note.)
  const std::string step = test::tempPath("really_step.igs");
  ASSERT_TRUE(test::writeStep(test::buildFixtureAssembly(false), step));
  const std::string iges = test::tempPath("really_iges.stp");
  ASSERT_TRUE(test::writeIges(blockAndPin(), iges));

  const core::ImportResult fromIges = core::importModel(iges);
  ASSERT_TRUE(fromIges.ok) << fromIges.error;
  EXPECT_EQ(fromIges.document->format(), core::FileFormat::Iges);

  // Reading STEP after IGES in one process keeps its structure and colors.
  const core::ImportResult fromStep = core::importModel(step);
  ASSERT_TRUE(fromStep.ok) << fromStep.error;
  EXPECT_EQ(fromStep.document->format(), core::FileFormat::Step);
  const core::AssemblyIndex index(fromStep.document);
  ASSERT_EQ(index.partInstances().size(), 5U);
  const core::AssemblyNode& greenPin = index.node(index.partInstances()[2]); // colored per instance
  ASSERT_TRUE(greenPin.inheritedColor);
  EXPECT_TRUE(greenPin.inheritedColor->GetRGB().IsEqual(Quantity_Color(Quantity_NOC_GREEN)));

  const std::string text = test::tempPath("notes.txt");
  writeAll(text, "Shopping list\nmilk\n");
  const core::ImportResult fromText = core::importModel(text);
  EXPECT_FALSE(fromText.ok);
  EXPECT_EQ(fromText.error, "The file is not a STEP or IGES file.");
}

TEST(IgesImport, AGzippedIgesFileOpens) {
  const std::string plain = test::tempPath("zipped.igs");
  ASSERT_TRUE(test::writeIges(blockAndPin(), plain));
  const std::string bytes = readAll(plain);
  const std::string gz = plain + ".gz";
  gzFile out = gzopen(gz.c_str(), "wb");
  ASSERT_NE(out, nullptr);
  ASSERT_EQ(gzwrite(out, bytes.data(), static_cast<unsigned>(bytes.size())), static_cast<int>(bytes.size()));
  gzclose(out);
  const core::ImportResult result = core::importModel(gz);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.document->format(), core::FileFormat::Iges);
  EXPECT_EQ(core::AssemblyIndex(result.document).partInstances().size(), 2U);
}

TEST(IgesImport, TheCacheKeepsTheFormat) {
  const std::string path = test::tempPath("cached.igs");
  ASSERT_TRUE(test::writeIges(blockAndPin(), path));
  core::ImportOptions options;
  options.cacheDir = test::tempPath("iges-cache");
  const core::ImportResult first = core::importModel(path, options);
  ASSERT_TRUE(first.ok) << first.error;
  EXPECT_FALSE(first.stats.fromCache);
  const core::ImportResult second = core::importModel(path, options);
  ASSERT_TRUE(second.ok) << second.error;
  EXPECT_TRUE(second.stats.fromCache);
  EXPECT_EQ(second.document->format(), core::FileFormat::Iges);
  EXPECT_EQ(second.document->fileUnit(), core::LengthUnit::Millimeter);
  EXPECT_EQ(core::AssemblyIndex(second.document).node(0).name, "Block");
}

TEST(FileFormat, NamesExtensionsAndUnits) {
  EXPECT_EQ(core::formatFromExtension("/x/Part.STEP"), core::FileFormat::Step);
  EXPECT_EQ(core::formatFromExtension("part.stp.gz"), core::FileFormat::Step);
  EXPECT_EQ(core::formatFromExtension("part.stpz"), core::FileFormat::Step);
  EXPECT_EQ(core::formatFromExtension("part.IGS"), core::FileFormat::Iges);
  EXPECT_EQ(core::formatFromExtension("part.iges.gz"), core::FileFormat::Iges);
  EXPECT_EQ(core::formatFromExtension("part.stl"), core::FileFormat::Stl);
  EXPECT_EQ(core::formatFromExtension("part.glb"), core::FileFormat::Gltf);
  EXPECT_EQ(core::formatFromExtension("part.ply"), core::FileFormat::Unknown);
  EXPECT_STREQ(core::displayName(core::FileFormat::Iges), "IGES");
  EXPECT_EQ(core::lengthUnitFromIgesFlag(1), core::LengthUnit::Inch);
  EXPECT_EQ(core::lengthUnitFromIgesFlag(2), core::LengthUnit::Millimeter);
  EXPECT_EQ(core::lengthUnitFromIgesFlag(6), core::LengthUnit::Meter);
  EXPECT_EQ(core::lengthUnitFromIgesFlag(10), core::LengthUnit::Centimeter);
  EXPECT_FALSE(core::lengthUnitFromIgesFlag(3)); // named in the next parameter
  EXPECT_FALSE(core::lengthUnitFromIgesFlag(8)); // mils
}
