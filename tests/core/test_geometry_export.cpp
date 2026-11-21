// File > Export Geometry (docs/designs/geometry-export.md): STL, OBJ and glTF written from the
// fixture assembly, read back and checked.

#include "TestModels.h"
#include "core/AssemblyIndex.h"
#include "core/GeometryExport.h"
#include "core/OccCompat.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Poly_Triangulation.hxx>
#include <RWGltf_CafReader.hxx>
#include <RWMesh_CoordinateSystem.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace sc;
namespace fs = std::filesystem;

namespace {

constexpr double Pi = 3.14159265358979323846;

// The importer meshes every part at the viewer's quality; do the same for the fixture.
std::shared_ptr<core::AssemblyIndex> meshedFixture() {
  const core::DocumentPtr document = test::buildFixtureAssembly();
  occ::LabelSequence labels;
  XCAFDoc_DocumentTool::ShapeTool(document->ocaf()->Main())->GetShapes(labels);
  for (const TDF_Label& label : labels) {
    if (!XCAFDoc_ShapeTool::IsAssembly(label)) {
      const TopoDS_Shape part = XCAFDoc_ShapeTool::GetShape(label);
      const auto [deflection, angle] = core::meshParameters(part, core::GeometryExportOptions());
      BRepMesh_IncrementalMesh mesher(part, deflection, false, angle);
    }
  }
  return std::make_shared<core::AssemblyIndex>(document);
}

core::NodeId findNode(const core::AssemblyIndex& index, const std::string& name) {
  for (const core::AssemblyNode& node : index.nodes()) {
    if (node.name == name) {
      return node.id;
    }
  }
  return core::NoNode;
}

fs::path freshDir(const std::string& name) {
  const fs::path dir = fs::path(test::tempPath(name));
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir;
}

std::string readText(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

struct Stl {
  std::uint32_t count = 0;
  std::vector<float> values; // per triangle: normal xyz, then three vertices xyz
  bool ok = false;

  double minOf(int axis) const { return extreme(axis, true); }
  double maxOf(int axis) const { return extreme(axis, false); }

  double extreme(int axis, bool low) const {
    double out = low ? 1e300 : -1e300;
    for (size_t t = 0; t < count; ++t) {
      for (int v = 0; v < 3; ++v) {
        const double value = values[t * 12 + 3 + static_cast<size_t>(v) * 3 + static_cast<size_t>(axis)];
        out = low ? std::min(out, value) : std::max(out, value);
      }
    }
    return out;
  }

  // Volume enclosed by the triangles: positive when they face outward.
  double signedVolume() const {
    double volume = 0.0;
    for (size_t t = 0; t < count; ++t) {
      const float* p = &values[t * 12 + 3];
      const gp_Vec a(p[0], p[1], p[2]), b(p[3], p[4], p[5]), c(p[6], p[7], p[8]);
      volume += a.Dot(b.Crossed(c)) / 6.0;
    }
    return volume;
  }
};

Stl readStl(const fs::path& file) {
  Stl stl;
  std::ifstream in(file, std::ios::binary);
  char header[84];
  if (!in.read(header, sizeof header) || std::strncmp(header, "solid", 5) == 0) {
    return stl;
  }
  std::memcpy(&stl.count, header + 80, 4); // little-endian, as the test hosts are
  stl.values.resize(static_cast<size_t>(stl.count) * 12);
  char record[50];
  for (size_t t = 0; t < stl.count; ++t) {
    if (!in.read(record, sizeof record)) {
      return stl;
    }
    std::memcpy(&stl.values[t * 12], record, 48);
  }
  stl.ok = in.peek() == std::char_traits<char>::eof();
  return stl;
}

// Triangles of the meshes on a shape's faces, and the meshes themselves.
std::vector<OccHandle<Poly_Triangulation>> faceMeshes(const TopoDS_Shape& shape) {
  std::vector<OccHandle<Poly_Triangulation>> meshes;
  for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
    TopLoc_Location location;
    meshes.push_back(BRep_Tool::Triangulation(TopoDS::Face(it.Current()), location));
  }
  return meshes;
}

std::uint32_t triangleCount(const core::AssemblyIndex& index, const std::vector<core::NodeId>& parts) {
  std::uint32_t count = 0;
  for (const core::NodeId part : parts) {
    for (const auto& mesh : faceMeshes(index.prototypeShape(part))) {
      count += static_cast<std::uint32_t>(mesh->NbTriangles());
    }
  }
  return count;
}

core::GeometryExportResult exportAll(const core::AssemblyIndex& index, const fs::path& path,
                                     const core::GeometryExportOptions& options) {
  return core::exportGeometry(index, index.roots(), path.string(), options);
}

core::GeometryExportOptions withFormat(core::GeometryFormat format) {
  core::GeometryExportOptions options;
  options.format = format;
  return options;
}

// Bounding box of the "v x y z" lines of an OBJ file.
Bnd_Box objBox(const std::string& text) {
  Bnd_Box box;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.rfind("v ", 0) == 0) {
      std::istringstream values(line.substr(2));
      double x = 0, y = 0, z = 0;
      values >> x >> y >> z;
      box.Add(gp_Pnt(x, y, z));
    }
  }
  return box;
}

// Reads a glTF/GLB file back into a new document, in millimetres and Z up.
core::DocumentPtr readGltf(const fs::path& file) {
  auto document = std::make_shared<core::Document>();
  RWGltf_CafReader reader;
  reader.SetDocument(document->ocaf());
  reader.SetSystemLengthUnit(0.001);
  reader.SetSystemCoordinateSystem(RWMesh_CoordinateSystem_Zup);
  if (!reader.Perform(TCollection_AsciiString(file.string().c_str()), Message_ProgressRange())) {
    return {};
  }
  return document;
}

Bnd_Box meshBox(const core::AssemblyIndex& index) {
  Bnd_Box box;
  for (const core::NodeId part : index.partInstances()) {
    for (TopExp_Explorer it(index.worldShape(part), TopAbs_FACE); it.More(); it.Next()) {
      TopLoc_Location location;
      const auto mesh = BRep_Tool::Triangulation(TopoDS::Face(it.Current()), location);
      for (int i = 1; mesh && i <= mesh->NbNodes(); ++i) {
        box.Add(mesh->Node(i).Transformed(location.Transformation()));
      }
    }
  }
  return box;
}

void expectBox(const Bnd_Box& box, gp_Pnt low, gp_Pnt high, double tolerance) {
  ASSERT_FALSE(box.IsVoid());
  const gp_Pnt min = box.CornerMin(), max = box.CornerMax();
  EXPECT_NEAR(min.X(), low.X(), tolerance);
  EXPECT_NEAR(min.Y(), low.Y(), tolerance);
  EXPECT_NEAR(min.Z(), low.Z(), tolerance);
  EXPECT_NEAR(max.X(), high.X(), tolerance);
  EXPECT_NEAR(max.Y(), high.Y(), tolerance);
  EXPECT_NEAR(max.Z(), high.Z(), tolerance);
}

} // namespace

TEST(GeometryExport, MergedStlPlacesEveryInstanceFacingOutward) {
  const auto index = meshedFixture();
  const fs::path file = freshDir("stl-merged") / "fixture.stl";
  const auto result = exportAll(*index, file, withFormat(core::GeometryFormat::StlMerged));
  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_EQ(result.files, std::vector<std::string>{file.string()});

  const Stl stl = readStl(file);
  ASSERT_TRUE(stl.ok);
  EXPECT_EQ(stl.count, triangleCount(*index, index->partInstances()));
  // Plate 100 x 60 x 10 at the origin; pins (r 3, h 20) on top at x = 10, 30, 50, 70, y = 10.
  EXPECT_NEAR(stl.minOf(0), 0.0, 1e-4);
  EXPECT_NEAR(stl.maxOf(0), 100.0, 1e-4);
  EXPECT_NEAR(stl.maxOf(1), 60.0, 1e-4);
  EXPECT_NEAR(stl.maxOf(2), 30.0, 1e-4);
  const double volume = 100.0 * 60.0 * 10.0 + 4 * Pi * 9.0 * 20.0;
  EXPECT_NEAR(stl.signedVolume(), volume, 0.02 * volume); // positive: normals face outward
}

TEST(GeometryExport, StlUnitScalesTheCoordinates) {
  const auto index = meshedFixture();
  const fs::path file = freshDir("stl-inch") / "fixture.stl";
  auto options = withFormat(core::GeometryFormat::StlMerged);
  options.unit = core::LengthUnit::Inch;
  ASSERT_TRUE(exportAll(*index, file, options).ok);
  const Stl stl = readStl(file);
  EXPECT_NEAR(stl.maxOf(0), 100.0 / 25.4, 1e-4);
  EXPECT_NEAR(stl.maxOf(2), 30.0 / 25.4, 1e-4);
}

TEST(GeometryExport, PerPartStlWritesEachUniquePartInItsOwnCoordinates) {
  const auto index = meshedFixture();
  const fs::path dir = freshDir("stl-parts") / "parts"; // created by the export
  const auto result = exportAll(*index, dir, withFormat(core::GeometryFormat::StlPerPart));
  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_EQ(result.files.size(), 2U); // 5 instances, 2 unique parts

  const Stl plate = readStl(dir / "Base plate.stl");
  ASSERT_TRUE(plate.ok);
  EXPECT_NEAR(plate.maxOf(0), 100.0, 1e-4);
  const Stl pin = readStl(dir / fs::u8path("Pin \xC3\x98"
                                           "6.stl"));
  ASSERT_TRUE(pin.ok);
  EXPECT_NEAR(pin.minOf(0), -3.0, 1e-3); // not placed in the assembly
  EXPECT_NEAR(pin.minOf(2), 0.0, 1e-4);
  EXPECT_NEAR(pin.maxOf(2), 20.0, 1e-4);
  EXPECT_GT(pin.signedVolume(), 0.0);
}

TEST(GeometryExport, PerPartFileNamesAreSafeAndUnique) {
  EXPECT_EQ(core::safeFileName("Bracket: left/right?"), "Bracket_ left_right_");
  EXPECT_EQ(core::safeFileName("  .hidden. "), "hidden");
  EXPECT_EQ(core::safeFileName("..."), "part");
  EXPECT_EQ(core::safeFileName("CON"), "CON_");
  EXPECT_EQ(core::safeFileName("Pin \xC3\x98"
                               "6"),
            "Pin \xC3\x98"
            "6");
  const std::vector<std::string> names{"Bolt", "bolt", "Nut", "Bolt", "a/b", "a_b"};
  const std::vector<std::string> expected{"Bolt.stl",   "bolt-2.stl", "Nut.stl",
                                          "Bolt-3.stl", "a_b.stl",    "a_b-2.stl"};
  EXPECT_EQ(core::perPartFileNames(names), expected);
}

TEST(GeometryExport, OnlyTheChosenPartsAreWritten) {
  const auto index = meshedFixture();
  const core::NodeId plate = findNode(*index, "Base plate");
  ASSERT_NE(plate, core::NoNode);
  const fs::path dir = freshDir("chosen");

  const fs::path stlFile = dir / "plate.stl";
  ASSERT_TRUE(
      core::exportGeometry(*index, {plate}, stlFile.string(), withFormat(core::GeometryFormat::StlMerged))
          .ok);
  EXPECT_EQ(readStl(stlFile).count, triangleCount(*index, {plate}));

  const fs::path glbFile = dir / "plate.glb";
  ASSERT_TRUE(
      core::exportGeometry(*index, {plate}, glbFile.string(), withFormat(core::GeometryFormat::Glb)).ok);
  const core::DocumentPtr back = readGltf(glbFile);
  ASSERT_TRUE(back);
  const core::AssemblyIndex backIndex(back);
  EXPECT_EQ(backIndex.partInstances().size(), 1U);
}

TEST(GeometryExport, ObjKeepsNamesAndColorsWithYUpByDefault) {
  const auto index = meshedFixture();
  const fs::path dir = freshDir("obj");
  const fs::path file = dir / "fixture.obj";
  const auto result = exportAll(*index, file, withFormat(core::GeometryFormat::Obj));
  ASSERT_TRUE(result.ok) << result.error;
  const fs::path mtl = dir / "fixture.mtl";
  EXPECT_EQ(result.files, (std::vector<std::string>{file.string(), mtl.string()}));

  const std::string obj = readText(file);
  EXPECT_NE(obj.find("Base plate"), std::string::npos);
  EXPECT_NE(obj.find("mtllib fixture.mtl"), std::string::npos);
  EXPECT_NE(readText(mtl).find("Kd 1.000000 0.000000 0.000000"), std::string::npos); // the red plate
  // Y up: the model's Z (0..30 mm) becomes Y, and its Y (0..60) becomes -Z.
  expectBox(objBox(obj), gp_Pnt(0, 0, -60), gp_Pnt(100, 30, 0), 1e-4);
}

TEST(GeometryExport, ObjCanKeepZUpAndUseAUnit) {
  const auto index = meshedFixture();
  const fs::path file = freshDir("obj-z") / "fixture.obj";
  auto options = withFormat(core::GeometryFormat::Obj);
  options.objUpAxis = core::UpAxis::Z;
  options.unit = core::LengthUnit::Centimeter;
  ASSERT_TRUE(exportAll(*index, file, options).ok);
  expectBox(objBox(readText(file)), gp_Pnt(0, 0, 0), gp_Pnt(10, 6, 3), 1e-5);
}

TEST(GeometryExport, GltfAndGlbKeepTheAssemblyAndShareMeshes) {
  const auto index = meshedFixture();
  const fs::path dir = freshDir("gltf");
  for (const auto format : {core::GeometryFormat::Gltf, core::GeometryFormat::Glb}) {
    const fs::path file = dir / ("fixture" + core::fileExtension(format));
    const auto result = exportAll(*index, file, withFormat(format));
    ASSERT_TRUE(result.ok) << result.error;
    if (format == core::GeometryFormat::Gltf) {
      const fs::path bin = dir / "fixture.bin";
      EXPECT_EQ(result.files, (std::vector<std::string>{file.string(), bin.string()}));
      const std::string json = readText(file);
      // Node names as in the Assembly panel, never XCAF's automatic "=>[0:1:1:2]".
      EXPECT_NE(json.find("\"Pin pair\""), std::string::npos);
      EXPECT_NE(json.find("\"Base plate\""), std::string::npos);
      EXPECT_EQ(json.find("=>["), std::string::npos);
      // Five instances, three sets of triangles: the plate, the plain pins (shared by three
      // instances) and the green pin (faces are merged per color).
      std::set<std::string> positions;
      const std::regex position("\"POSITION\":(\\d+)");
      for (auto it = std::sregex_iterator(json.begin(), json.end(), position); it != std::sregex_iterator();
           ++it) {
        positions.insert((*it)[1]);
      }
      EXPECT_EQ(positions.size(), 3U);
    } else {
      EXPECT_EQ(result.files, std::vector<std::string>{file.string()});
    }

    const core::DocumentPtr back = readGltf(file);
    ASSERT_TRUE(back) << file;
    const core::AssemblyIndex backIndex(back);
    EXPECT_EQ(backIndex.partInstances().size(), 5U);
    // Written in metres, Y up; read back in millimetres, Z up: the same box as the model.
    expectBox(meshBox(backIndex), gp_Pnt(0, 0, 0), gp_Pnt(100, 60, 30), 1e-3);
  }
}

TEST(GeometryExport, FinerQualityMeshesACopyAndLeavesTheModelAlone) {
  const auto index = meshedFixture();
  std::vector<OccHandle<Poly_Triangulation>> before;
  for (const core::NodeId part : index->partInstances()) {
    for (const auto& mesh : faceMeshes(index->prototypeShape(part))) {
      before.push_back(mesh);
    }
  }
  const std::uint32_t displayTriangles = triangleCount(*index, index->partInstances());

  const fs::path dir = freshDir("fine");
  std::uint32_t previous = displayTriangles;
  for (const auto quality : {core::MeshQuality::Fine, core::MeshQuality::ExtraFine}) {
    const fs::path file = dir / (std::to_string(static_cast<int>(quality)) + ".stl");
    auto options = withFormat(core::GeometryFormat::StlMerged);
    options.quality = quality;
    ASSERT_TRUE(exportAll(*index, file, options).ok);
    const Stl stl = readStl(file);
    EXPECT_GT(stl.count, previous);
    EXPECT_NEAR(stl.maxOf(0), 100.0, 1e-4);
    previous = stl.count;
  }

  std::vector<OccHandle<Poly_Triangulation>> after;
  for (const core::NodeId part : index->partInstances()) {
    for (const auto& mesh : faceMeshes(index->prototypeShape(part))) {
      after.push_back(mesh);
    }
  }
  EXPECT_EQ(before, after); // the very same mesh objects
  EXPECT_EQ(triangleCount(*index, index->partInstances()), displayTriangles);
}

TEST(GeometryExport, CustomToleranceIsInTheExportUnit) {
  core::GeometryExportOptions options;
  options.quality = core::MeshQuality::Custom;
  options.unit = core::LengthUnit::Inch;
  options.customTolerance = 0.01;
  options.customAngleDegrees = 3.0;
  const auto [deflection, angle] = core::meshParameters(TopoDS_Shape(), options);
  EXPECT_NEAR(deflection, 0.254, 1e-9);
  EXPECT_NEAR(angle, 3.0 * Pi / 180.0, 1e-9);
}

TEST(GeometryExport, CancelLeavesNoFiles) {
  const auto index = meshedFixture();
  const fs::path dir = freshDir("cancel");
  core::CancelToken cancel;
  cancel.cancel();
  for (const auto format :
       {core::GeometryFormat::StlMerged, core::GeometryFormat::Obj, core::GeometryFormat::Gltf}) {
    for (const auto quality : {core::MeshQuality::AsDisplayed, core::MeshQuality::Fine}) {
      auto options = withFormat(format);
      options.quality = quality;
      const fs::path file = dir / ("fixture" + core::fileExtension(format));
      const auto result = core::exportGeometry(*index, index->roots(), file.string(), options, {}, &cancel);
      EXPECT_FALSE(result.ok);
      EXPECT_TRUE(result.cancelled);
      EXPECT_TRUE(result.error.empty());
    }
  }
  EXPECT_TRUE(fs::is_empty(dir));
}

TEST(GeometryExport, UnwritablePathIsAnError) {
  const auto index = meshedFixture();
  const fs::path dir = freshDir("unwritable");
  const fs::path blocker = dir / "file";
  std::ofstream(blocker) << "x"; // a file where a folder is needed
  for (const auto format : {core::GeometryFormat::StlMerged, core::GeometryFormat::StlPerPart,
                            core::GeometryFormat::Obj, core::GeometryFormat::Glb}) {
    const fs::path target = blocker / ("out" + core::fileExtension(format));
    const auto result = exportAll(*index, target, withFormat(format));
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.cancelled);
    EXPECT_FALSE(result.error.empty());
  }
  EXPECT_FALSE(core::exportGeometry(*index, {}, (dir / "none.stl").string(),
                                    withFormat(core::GeometryFormat::StlMerged))
                   .ok);
}
