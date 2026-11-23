// Mesh import (docs/designs/mesh-import.md): STL, OBJ and glTF read into parts with names, colors,
// placement and the right units and axes; detection by content; mass properties from the mesh.

#include "TestModels.h"
#include "core/AssemblyIndex.h"
#include "core/FileFormat.h"
#include "core/GeometryExport.h"
#include "core/Measure.h"
#include "core/StepImporter.h"

#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Bnd_Box.hxx>
#include <StlAPI_Writer.hxx>
#include <TopExp_Explorer.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_VisMaterialTool.hxx>

#include <gtest/gtest.h>

#include <filesystem>

using namespace sc;

namespace {

std::string writeBoxStl(const std::string& name, bool ascii) {
  const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();
  BRepMesh_IncrementalMesh mesher(box, 0.1);
  const std::string path = test::tempPath(name);
  StlAPI_Writer writer;
  writer.ASCIIMode() = ascii;
  EXPECT_TRUE(writer.Write(box, path.c_str()));
  return path;
}

Bnd_Box boxOf(const core::AssemblyIndex& index, const std::vector<core::NodeId>& parts) {
  Bnd_Box box;
  for (const core::NodeId part : parts) {
    BRepBndLib::Add(index.worldShape(part), box, true);
  }
  return box;
}

void expectSameBox(const Bnd_Box& actual, const Bnd_Box& expected, double tolerance) {
  ASSERT_FALSE(actual.IsVoid());
  EXPECT_NEAR(actual.CornerMin().X(), expected.CornerMin().X(), tolerance);
  EXPECT_NEAR(actual.CornerMin().Y(), expected.CornerMin().Y(), tolerance);
  EXPECT_NEAR(actual.CornerMin().Z(), expected.CornerMin().Z(), tolerance);
  EXPECT_NEAR(actual.CornerMax().X(), expected.CornerMax().X(), tolerance);
  EXPECT_NEAR(actual.CornerMax().Y(), expected.CornerMax().Y(), tolerance);
  EXPECT_NEAR(actual.CornerMax().Z(), expected.CornerMax().Z(), tolerance);
}

// The fixture exported with the app's own exporter, and its world box.
std::string exported(core::GeometryFormat format, const std::string& name, Bnd_Box& expected) {
  const core::AssemblyIndex index(test::buildFixtureAssembly(false));
  expected = boxOf(index, index.partInstances());
  core::GeometryExportOptions options;
  options.format = format;
  options.quality = core::MeshQuality::Fine;
  const std::string path = test::tempPath(name);
  const core::GeometryExportResult result = core::exportGeometry(index, index.roots(), path, options);
  EXPECT_TRUE(result.ok) << result.error;
  return path;
}

bool hasNamedPart(const core::AssemblyIndex& index, const std::string& name) {
  for (const core::NodeId part : index.partInstances()) {
    if (index.node(part).name == name) {
      return true;
    }
  }
  return false;
}

// The fixture's plate is red: after a round trip its color is a plain surface color again, with
// no visual material left over (the viewer would wash it out).
void expectPlateIsRed(const core::ImportResult& result) {
  const OccHandle<XCAFDoc_ColorTool> colors =
      XCAFDoc_DocumentTool::ColorTool(result.document->ocaf()->Main());
  const OccHandle<XCAFDoc_VisMaterialTool> materials =
      XCAFDoc_DocumentTool::VisMaterialTool(result.document->ocaf()->Main());
  const core::AssemblyIndex index(result.document);
  bool red = false;
  for (const core::NodeId part : index.partInstances()) {
    const core::AssemblyNode& node = index.node(part);
    EXPECT_TRUE(materials->GetShapeMaterial(node.prototypeLabel).IsNull());
    Quantity_Color color;
    if (node.inheritedColor) {
      color = node.inheritedColor->GetRGB();
    } else if (!colors->GetColor(node.prototypeLabel, XCAFDoc_ColorSurf, color)) {
      continue;
    }
    red = red || (color.Red() > 0.9 && color.Green() < 0.1 && color.Blue() < 0.1);
  }
  EXPECT_TRUE(red);
}

} // namespace

TEST(MeshImport, BinaryAndAsciiStlReadAsOnePart) {
  for (const bool ascii : {false, true}) {
    const std::string path = writeBoxStl(ascii ? "box_ascii.stl" : "box_binary.stl", ascii);
    EXPECT_EQ(core::detectFileFormat(path), core::FileFormat::Stl);
    const core::ImportResult result = core::importModel(path);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.document->format(), core::FileFormat::Stl);
    EXPECT_EQ(result.document->fileUnitName(), "mm (assumed)");
    EXPECT_FALSE(result.document->fileUnit());

    const core::AssemblyIndex index(result.document);
    ASSERT_EQ(index.partInstances().size(), 1U);
    const core::NodeId part = index.partInstances().front();
    EXPECT_EQ(index.node(part).name, std::filesystem::u8path(path).stem().u8string())
        << "named after the file";
    Bnd_Box expected;
    expected.Update(0, 0, 0, 10, 20, 30);
    expectSameBox(boxOf(index, {part}), expected, 1e-4);

    // Mass properties come from the triangles: exact for this closed box.
    const core::measure::MassProperties mass = core::measure::massProperties({index.worldShape(part)});
    ASSERT_TRUE(mass.ok);
    EXPECT_TRUE(mass.fromMesh);
    EXPECT_TRUE(mass.hasVolume);
    EXPECT_NEAR(mass.volume, 6000.0, 1e-3);
    EXPECT_NEAR(mass.area, 2200.0, 1e-3);
    EXPECT_NEAR(mass.centroid.Z(), 15.0, 1e-6);

    // A mesh face has no plane to put a section on.
    const TopoDS_Shape face = TopExp_Explorer(index.worldShape(part), TopAbs_FACE).Current();
    EXPECT_FALSE(core::measure::planeOfFace(face));
  }
}

TEST(MeshImport, ObjFromTheExporterOpensTheRightWayUp) {
  Bnd_Box expected;
  const std::string path = exported(core::GeometryFormat::Obj, "fixture.obj", expected);
  const core::ImportResult result = core::importModel(path);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.document->format(), core::FileFormat::Obj);
  EXPECT_EQ(result.document->fileUnitName(), "mm (assumed)");
  const core::AssemblyIndex index(result.document);
  EXPECT_GE(index.partInstances().size(), 2U);
  // Exported Y up, read as Y up: the same box as the model, Z up.
  expectSameBox(boxOf(index, index.partInstances()), expected, 0.05);
  // The .mtl colors become plain part colors, as in STEP: the plate is red.
  expectPlateIsRed(result);
}

TEST(MeshImport, GlbFromTheExporterComesBackInMillimetres) {
  Bnd_Box expected;
  const std::string path = exported(core::GeometryFormat::Glb, "fixture.glb", expected);
  EXPECT_EQ(core::detectFileFormat(path), core::FileFormat::Gltf);
  const core::ImportResult result = core::importModel(path);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.document->format(), core::FileFormat::Gltf);
  EXPECT_EQ(result.document->fileUnit(), core::LengthUnit::Meter);
  const core::AssemblyIndex index(result.document);
  ASSERT_EQ(index.partInstances().size(), 5U); // the glTF nodes keep the instances
  EXPECT_TRUE(hasNamedPart(index, "Base plate"));
  expectSameBox(boxOf(index, index.partInstances()), expected, 0.05); // metres and Y up undone
  expectPlateIsRed(result);
}

TEST(MeshImport, TheContentDecidesAndTheCacheKeepsIt) {
  // A binary STL and a GLB with names that say nothing.
  const std::string stl = writeBoxStl("box.stl", false);
  const std::string misnamed = test::tempPath("download.dat");
  std::filesystem::copy_file(std::filesystem::u8path(stl), std::filesystem::u8path(misnamed));
  EXPECT_EQ(core::detectFileFormat(misnamed), core::FileFormat::Stl);
  Bnd_Box expected;
  const std::string glb = exported(core::GeometryFormat::Glb, "fixture.glb", expected);
  const std::string glbMisnamed = test::tempPath("scene.bin");
  std::filesystem::copy_file(std::filesystem::u8path(glb), std::filesystem::u8path(glbMisnamed));
  EXPECT_EQ(core::detectFileFormat(glbMisnamed), core::FileFormat::Gltf);

  core::ImportOptions options;
  options.cacheDir = test::tempPath("mesh-cache");
  ASSERT_TRUE(core::importModel(misnamed, options).ok);
  const core::ImportResult cached = core::importModel(misnamed, options);
  ASSERT_TRUE(cached.ok) << cached.error;
  EXPECT_TRUE(cached.stats.fromCache);
  EXPECT_EQ(cached.document->format(), core::FileFormat::Stl);
  const core::AssemblyIndex index(cached.document);
  EXPECT_TRUE(core::measure::hasMeshOnlyFaces(index.worldShape(index.partInstances().front())));
}

TEST(MeshImport, MeshFormatsAreMeshes) {
  EXPECT_TRUE(core::isMeshFormat(core::FileFormat::Stl));
  EXPECT_TRUE(core::isMeshFormat(core::FileFormat::Obj));
  EXPECT_TRUE(core::isMeshFormat(core::FileFormat::Gltf));
  EXPECT_FALSE(core::isMeshFormat(core::FileFormat::Step));
  EXPECT_FALSE(core::isMeshFormat(core::FileFormat::Iges));
  EXPECT_STREQ(core::displayName(core::FileFormat::Gltf), "glTF");
  EXPECT_FALSE(core::measure::hasMeshOnlyFaces(BRepPrimAPI_MakeBox(1, 1, 1).Shape()));
}
