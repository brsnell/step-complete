#include "core/GeometryExport.h"

#include "core/DocumentCache.h"
#include "core/OccCompat.h"
#include "core/ProgressBridge.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <IMeshTools_Parameters.hxx>
#include <OSD_Parallel.hxx>
#include <OSD_ThreadPool.hxx>
#include <Poly_Triangulation.hxx>
#include <Prs3d_Drawer.hxx>
#include <RWGltf_CafWriter.hxx>
#include <RWMesh_CoordinateSystem.hxx>
#include <RWMesh_CoordinateSystemConverter.hxx>
#include <RWObj_CafWriter.hxx>
#include <Standard_Failure.hxx>
#include <StdPrs_ToolTriangulatedShape.hxx>
#include <TDF_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFPrs_DocumentExplorer.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <unordered_set>

namespace sc::core {

namespace fs = std::filesystem;

namespace {

constexpr double Pi = 3.14159265358979323846;
constexpr double FineFactor = 0.25;
constexpr double ExtraFineFactor = 0.1;
constexpr double FineAngleDegrees = 10.0;
constexpr double ExtraFineAngleDegrees = 5.0;
constexpr std::uint64_t MaxStlTriangles = std::numeric_limits<std::uint32_t>::max();

double radians(double degrees) {
  return degrees * Pi / 180.0;
}

std::string toUtf8(const fs::path& path) {
  const auto text = path.u8string();
  return std::string(text.begin(), text.end());
}

bool isCancelled(const CancelToken* cancel) {
  return cancel != nullptr && cancel->isCancelled();
}

// The chosen part instances in tree order, each once.
std::vector<NodeId> chosenParts(const AssemblyIndex& index, const std::vector<NodeId>& nodes) {
  std::vector<char> chosen(index.nodes().size(), 0);
  for (const NodeId id : nodes) {
    if (id < 0 || static_cast<size_t>(id) >= index.nodes().size()) {
      continue;
    }
    for (const NodeId part : index.partsBelow(id)) {
      chosen[static_cast<size_t>(part)] = 1;
    }
  }
  std::vector<NodeId> parts;
  for (const NodeId id : index.partInstances()) {
    if (chosen[static_cast<size_t>(id)] != 0) {
      parts.push_back(id);
    }
  }
  return parts;
}

// The id OCCT's mesh writers give a node (XCAFPrs_DocumentExplorer's path of label entries),
// for their label filter.
TCollection_AsciiString explorerId(const AssemblyIndex& index, NodeId id) {
  const AssemblyNode& node = index.node(id);
  const TCollection_AsciiString parentId =
      node.parent == NoNode ? TCollection_AsciiString() : explorerId(index, node.parent);
  return XCAFPrs_DocumentExplorer::DefineChildId(node.instanceLabel, parentId);
}

// The same label in another document with the same structure (a copy made by copyDocument).
TDF_Label sameLabel(const TDF_Label& label, const OccHandle<TDocStd_Document>& doc) {
  TCollection_AsciiString entry;
  TDF_Tool::Entry(label, entry);
  TDF_Label out;
  TDF_Tool::Label(doc->GetData(), entry, out);
  return out;
}

// glTF node names as the Assembly panel shows them: the instance name, unless it is empty or one
// of XCAF's automatic "=>[0:1:1:2]" names, else the product name.
class GltfWriter : public RWGltf_CafWriter {
public:
  using RWGltf_CafWriter::RWGltf_CafWriter;

protected:
  TCollection_AsciiString formatName(RWMesh_NameFormat format, const TDF_Label& label,
                                     const TDF_Label& refLabel) const override {
    if (format != RWMesh_NameFormat_InstanceOrProduct) {
      return RWGltf_CafWriter::formatName(format, label, refLabel);
    }
    const TCollection_AsciiString instance =
        RWGltf_CafWriter::formatName(RWMesh_NameFormat_Instance, label, refLabel);
    if (!instance.IsEmpty() && !instance.StartsWith("=>")) {
      return instance;
    }
    return RWGltf_CafWriter::formatName(RWMesh_NameFormat_Product, label, refLabel);
  }
};

// --- Meshing the private copy ---------------------------------------------------

void meshShapes(const std::vector<TopoDS_Shape>& shapes, const GeometryExportOptions& options,
                const Message_ProgressRange& range, const CancelToken* cancel) {
  Message_ProgressScope scope(range, "Meshing", static_cast<double>(shapes.size()));
  std::vector<Message_ProgressRange> ranges;
  ranges.reserve(shapes.size());
  for (size_t i = 0; i < shapes.size(); ++i) {
    ranges.push_back(scope.Next());
  }
  // As in the importer: many parts mesh in parallel; few big parts parallelize over faces.
  const int threads = OSD_ThreadPool::DefaultPool()->NbThreads();
  const bool acrossParts = threads > 1 && shapes.size() >= static_cast<size_t>(2 * threads);
  const auto meshOne = [&](int i) {
    Message_ProgressScope partScope(ranges[static_cast<size_t>(i)], nullptr, 1.0);
    if (isCancelled(cancel) || !partScope.More()) {
      return;
    }
    const TopoDS_Shape& shape = shapes[static_cast<size_t>(i)];
    BRepTools::Clean(shape);
    const auto [deflection, angle] = meshParameters(shape, options);
    IMeshTools_Parameters parameters;
    parameters.Deflection = deflection;
    parameters.Angle = angle;
    parameters.InParallel = !acrossParts;
    BRepMesh_IncrementalMesh mesher(shape, parameters);
  };
  OSD_Parallel::For(0, static_cast<int>(shapes.size()), meshOne, !acrossParts);
}

// --- Binary STL -------------------------------------------------------------------

void putFloat(char*& out, double value) {
  const float single = static_cast<float>(value);
  std::uint32_t bits = 0;
  std::memcpy(&bits, &single, sizeof bits);
  for (int i = 0; i < 4; ++i) {
    *out++ = static_cast<char>((bits >> (8 * i)) & 0xFFu);
  }
}

void putUint32(char* out, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out[i] = static_cast<char>((value >> (8 * i)) & 0xFFu);
  }
}

// Triangles of the shapes' faces, and how many faces have no mesh.
std::uint64_t countTriangles(const std::vector<TopoDS_Shape>& shapes, int& skippedFaces) {
  std::uint64_t count = 0;
  for (const TopoDS_Shape& shape : shapes) {
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
      TopLoc_Location location;
      const OccHandle<Poly_Triangulation> mesh =
          BRep_Tool::Triangulation(TopoDS::Face(it.Current()), location);
      if (mesh.IsNull() || mesh->NbTriangles() == 0) {
        ++skippedFaces;
      } else {
        count += static_cast<std::uint64_t>(mesh->NbTriangles());
      }
    }
  }
  return count;
}

// Writes the shapes' face meshes (placed by the shapes' own locations) as one binary STL.
// 'scale' converts millimetres to the file's unit. Returns an error message, empty on success.
std::string writeStl(const fs::path& file, const std::vector<TopoDS_Shape>& shapes, double scale,
                     const Message_ProgressRange& range, const CancelToken* cancel, int& skippedFaces) {
  const std::uint64_t triangles = countTriangles(shapes, skippedFaces);
  if (triangles > MaxStlTriangles) {
    return "The model has too many triangles for one STL file.";
  }
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  if (!out) {
    return "Cannot write " + toUtf8(file.filename()) + ".";
  }
  // The header must not start with "solid": readers would take the file for ASCII STL.
  char header[84] = {};
  const char title[] = "Binary STL written by STEP Complete";
  std::memcpy(header, title, sizeof title - 1);
  putUint32(header + 80, static_cast<std::uint32_t>(triangles));
  out.write(header, sizeof header);

  Message_ProgressScope scope(range, "Writing", static_cast<double>(shapes.size()));
  char record[50];
  for (const TopoDS_Shape& shape : shapes) {
    if (isCancelled(cancel) || !scope.More()) {
      return {};
    }
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
      const TopoDS_Face& face = TopoDS::Face(it.Current());
      TopLoc_Location location;
      const OccHandle<Poly_Triangulation> mesh = BRep_Tool::Triangulation(face, location);
      if (mesh.IsNull() || mesh->NbTriangles() == 0) {
        continue;
      }
      const gp_Trsf trsf = location.Transformation();
      // Outward normals: reversed faces and mirroring placements flip the winding.
      const bool flip = (face.Orientation() == TopAbs_REVERSED) != trsf.IsNegative();
      for (int i = 1; i <= mesh->NbTriangles(); ++i) {
        int n1 = 0, n2 = 0, n3 = 0;
        mesh->Triangle(i).Get(n1, n2, n3);
        if (flip) {
          std::swap(n2, n3);
        }
        const gp_Pnt p1 = mesh->Node(n1).Transformed(trsf);
        const gp_Pnt p2 = mesh->Node(n2).Transformed(trsf);
        const gp_Pnt p3 = mesh->Node(n3).Transformed(trsf);
        gp_Vec normal = gp_Vec(p1, p2).Crossed(gp_Vec(p1, p3));
        const double length = normal.Magnitude();
        normal = length > 0.0 ? normal / length : gp_Vec(0.0, 0.0, 0.0);
        char* cursor = record;
        putFloat(cursor, normal.X());
        putFloat(cursor, normal.Y());
        putFloat(cursor, normal.Z());
        for (const gp_Pnt& point : {p1, p2, p3}) {
          putFloat(cursor, point.X() * scale);
          putFloat(cursor, point.Y() * scale);
          putFloat(cursor, point.Z() * scale);
        }
        cursor[0] = 0; // attribute byte count
        cursor[1] = 0;
        out.write(record, sizeof record);
      }
    }
    scope.Next();
  }
  out.flush();
  if (!out.good()) {
    return "Could not finish writing " + toUtf8(file.filename()) + " (is the disk full?).";
  }
  return {};
}

// --- File names for one STL per part ------------------------------------------------

std::string lowerAscii(std::string text) {
  for (char& c : text) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return text;
}

} // namespace

std::string safeFileName(const std::string& name) {
  std::string out;
  for (const char c : name) {
    const auto byte = static_cast<unsigned char>(c);
    out += (byte < 32 || std::strchr("<>:\"/\\|?*", c) != nullptr) ? '_' : c;
  }
  // Windows drops trailing dots and spaces; leading ones hide files elsewhere.
  while (!out.empty() && (out.back() == '.' || out.back() == ' ')) {
    out.pop_back();
  }
  while (!out.empty() && (out.front() == '.' || out.front() == ' ')) {
    out.erase(out.begin());
  }
  if (out.empty()) {
    return "part";
  }
  static const std::set<std::string> reserved = {
      "con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4", "com5", "com6", "com7",
      "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
  if (reserved.count(lowerAscii(out)) != 0) {
    out += '_';
  }
  return out;
}

bool writesFolder(GeometryFormat format) {
  return format == GeometryFormat::StlPerPart;
}

std::string fileExtension(GeometryFormat format) {
  switch (format) {
  case GeometryFormat::StlMerged:
  case GeometryFormat::StlPerPart:
    return ".stl";
  case GeometryFormat::Obj:
    return ".obj";
  case GeometryFormat::Gltf:
    return ".gltf";
  case GeometryFormat::Glb:
    return ".glb";
  }
  return {};
}

std::pair<double, double> meshParameters(const TopoDS_Shape& part, const GeometryExportOptions& options) {
  OccHandle<Prs3d_Drawer> drawer = new Prs3d_Drawer();
  const double displayDeflection = StdPrs_ToolTriangulatedShape::GetDeflection(part, drawer);
  const double displayAngle = drawer->DeviationAngle();
  switch (options.quality) {
  case MeshQuality::AsDisplayed:
    break;
  case MeshQuality::Fine:
    return {displayDeflection * FineFactor, std::min(displayAngle, radians(FineAngleDegrees))};
  case MeshQuality::ExtraFine:
    return {displayDeflection * ExtraFineFactor, std::min(displayAngle, radians(ExtraFineAngleDegrees))};
  case MeshQuality::Custom: {
    const double tolerance = toMillimeters(options.customTolerance, options.unit);
    const double angle = std::clamp(options.customAngleDegrees, 0.5, 90.0);
    return {std::max(tolerance, 1e-4), radians(angle)};
  }
  }
  return {displayDeflection, displayAngle};
}

std::vector<std::string> perPartFileNames(const std::vector<std::string>& names) {
  std::vector<std::string> out;
  std::set<std::string> used; // case-insensitive: Windows and macOS file systems are
  for (const std::string& name : names) {
    const std::string base = safeFileName(name);
    std::string candidate = base;
    for (int n = 2; used.count(lowerAscii(candidate)) != 0; ++n) {
      candidate = base + "-" + std::to_string(n);
    }
    used.insert(lowerAscii(candidate));
    out.push_back(candidate + ".stl");
  }
  return out;
}

GeometryExportResult exportGeometry(const AssemblyIndex& index, const std::vector<NodeId>& nodes,
                                    const std::string& utf8Path, const GeometryExportOptions& options,
                                    const ProgressCallback& progress, const CancelToken* cancel) {
  GeometryExportResult result;
  const DocumentPtr& source = index.document();
  if (!source || source->ocaf().IsNull()) {
    result.error = "No model is open.";
    return result;
  }
  const std::vector<NodeId> parts = chosenParts(index, nodes);
  if (parts.empty()) {
    result.error = "There are no parts to export.";
    return result;
  }

  const fs::path target = fs::u8path(utf8Path);
  std::vector<fs::path> written; // removed again unless the export succeeds
  const auto fail = [&](std::string error) {
    std::error_code ec;
    for (const fs::path& file : written) {
      fs::remove(file, ec);
    }
    result.files.clear();
    result.ok = false;
    result.cancelled = isCancelled(cancel);
    result.error = result.cancelled ? std::string() : std::move(error);
    return result;
  };

  std::lock_guard<std::mutex> lock(xcafMutex());
  DocumentPtr copy; // destroyed before the lock is released (declared after it)
  try {
    OccHandle<ProgressBridge> bridge = new ProgressBridge(progress, cancel);
    Message_ProgressScope root(bridge->Start(), "Export", 100.0);

    // Finer qualities mesh a private copy; the open model's meshes stay as they are.
    OccHandle<TDocStd_Document> doc = source->ocaf();
    if (options.quality != MeshQuality::AsDisplayed) {
      bridge->setStage("Copying the model");
      copy = copyDocument(*source, false);
      if (!copy) {
        return fail("Could not copy the model for meshing.");
      }
      doc = copy->ocaf();
    }
    root.Next(10.0);
    if (isCancelled(cancel)) {
      return fail({});
    }

    // Each chosen part's prototype shape in the document being written, each TShape once.
    std::vector<TDF_Label> prototypes(parts.size());
    std::vector<TopoDS_Shape> uniqueShapes;
    std::unordered_set<const void*> seen;
    for (size_t i = 0; i < parts.size(); ++i) {
      const TDF_Label label = index.node(parts[i]).prototypeLabel;
      prototypes[i] = copy ? sameLabel(label, doc) : label;
      const TopoDS_Shape shape = XCAFDoc_ShapeTool::GetShape(prototypes[i]);
      if (!shape.IsNull() && seen.insert(shape.TShape().get()).second) {
        uniqueShapes.push_back(shape);
      }
    }
    if (copy) {
      bridge->setStage("Meshing");
      meshShapes(uniqueShapes, options, root.Next(50.0), cancel);
      if (isCancelled(cancel)) {
        return fail({});
      }
    } else {
      root.Next(50.0);
    }

    bridge->setStage("Writing");
    const Message_ProgressRange writeRange = root.Next(40.0);
    const double scale = 1.0 / millimetersPer(options.unit);
    switch (options.format) {
    case GeometryFormat::StlMerged: {
      std::vector<TopoDS_Shape> placed;
      placed.reserve(parts.size());
      for (size_t i = 0; i < parts.size(); ++i) {
        TopoDS_Shape shape = XCAFDoc_ShapeTool::GetShape(prototypes[i]);
        shape.Location(index.node(parts[i]).location * shape.Location()); // as AssemblyIndex::worldShape
        placed.push_back(shape);
      }
      written.push_back(target);
      const std::string error = writeStl(target, placed, scale, writeRange, cancel, result.skippedFaces);
      if (!error.empty() || isCancelled(cancel)) {
        return fail(error);
      }
      break;
    }
    case GeometryFormat::StlPerPart: {
      std::error_code ec;
      fs::create_directories(target, ec);
      if (!fs::is_directory(target)) {
        return fail("Cannot create the folder " + toUtf8(target) + ".");
      }
      // One file per unique prototype, in the order the parts first appear.
      std::vector<TopoDS_Shape> shapes;
      std::vector<std::string> names;
      std::set<std::string> done;
      for (size_t i = 0; i < parts.size(); ++i) {
        TCollection_AsciiString entry;
        TDF_Tool::Entry(prototypes[i], entry);
        if (!done.insert(entry.ToCString()).second) {
          continue;
        }
        const AssemblyNode& node = index.node(parts[i]);
        shapes.push_back(XCAFDoc_ShapeTool::GetShape(prototypes[i]));
        names.push_back(node.prototypeName.empty() ? node.name : node.prototypeName);
      }
      const std::vector<std::string> fileNames = perPartFileNames(names);
      Message_ProgressScope scope(writeRange, "Writing", static_cast<double>(shapes.size()));
      for (size_t i = 0; i < shapes.size(); ++i) {
        const fs::path file = target / fs::u8path(fileNames[i]);
        written.push_back(file);
        const std::string error =
            writeStl(file, {shapes[i]}, scale, scope.Next(), cancel, result.skippedFaces);
        if (!error.empty() || isCancelled(cancel)) {
          return fail(error);
        }
      }
      break;
    }
    case GeometryFormat::Obj:
    case GeometryFormat::Gltf:
    case GeometryFormat::Glb: {
      occ::AsciiStringMap filter;
      for (const NodeId part : parts) {
        for (NodeId id = part; id != NoNode; id = index.node(id).parent) {
          filter.Add(explorerId(index, id));
        }
      }
      occ::LabelSequence roots;
      XCAFDoc_DocumentTool::ShapeTool(doc->Main())->GetFreeShapes(roots);
      const occ::StringStringMap fileInfo;
      const TCollection_AsciiString file(toUtf8(target).c_str());
      bool ok = false;
      if (options.format == GeometryFormat::Obj) {
        written.push_back(target);
        written.push_back(fs::path(target).replace_extension(".mtl"));
        RWObj_CafWriter writer(file);
        RWMesh_CoordinateSystemConverter& converter = writer.ChangeCoordinateSystemConverter();
        converter.SetInputLengthUnit(0.001);
        converter.SetInputCoordinateSystem(RWMesh_CoordinateSystem_Zup);
        converter.SetOutputLengthUnit(millimetersPer(options.unit) * 0.001);
        converter.SetOutputCoordinateSystem(options.objUpAxis == UpAxis::Y ? RWMesh_CoordinateSystem_Yup
                                                                           : RWMesh_CoordinateSystem_Zup);
        ok = writer.Perform(doc, roots, &filter, fileInfo, writeRange);
      } else {
        const bool binary = options.format == GeometryFormat::Glb;
        written.push_back(target);
        if (!binary) {
          written.push_back(fs::path(target).replace_extension(".bin"));
        }
        GltfWriter writer(file, binary);
        writer.SetNodeNameFormat(RWMesh_NameFormat_InstanceOrProduct);
        RWMesh_CoordinateSystemConverter& converter = writer.ChangeCoordinateSystemConverter();
        converter.SetInputLengthUnit(0.001);
        converter.SetInputCoordinateSystem(RWMesh_CoordinateSystem_Zup);
        converter.SetOutputLengthUnit(1.0);
        converter.SetOutputCoordinateSystem(RWMesh_CoordinateSystem_glTF);
        writer.SetMergeFaces(true);
        ok = writer.Perform(doc, roots, &filter, fileInfo, writeRange);
      }
      if (!ok || isCancelled(cancel)) {
        return fail("Open CASCADE could not write " + toUtf8(target.filename()) + ".");
      }
      break;
    }
    }

    bridge->report(1.0, true);
    std::error_code ec;
    for (const fs::path& file : written) {
      if (fs::exists(file, ec)) {
        result.files.push_back(toUtf8(file));
      }
    }
    result.ok = true;
    copy.reset();
    return result;
  } catch (const Standard_Failure& failure) {
    copy.reset();
    return fail("Open CASCADE error: " + occ::errorMessage(failure));
  } catch (const std::exception& error) {
    copy.reset();
    return fail(std::string("Error: ") + error.what());
  }
}

} // namespace sc::core
