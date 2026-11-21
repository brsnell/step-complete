#pragma once

#include "core/AssemblyIndex.h"
#include "core/StepImporter.h"
#include "core/Units.h"

#include <string>
#include <utility>
#include <vector>

namespace sc::core {

// File > Export Geometry: writes part meshes as STL, OBJ or glTF.
// Design: docs/designs/geometry-export.md.

enum class GeometryFormat {
  StlMerged,  // one binary STL, every chosen instance in place (world coordinates)
  StlPerPart, // one binary STL per unique part, in the part's own coordinates
  Obj,        // OBJ plus a .mtl file with the colors
  Gltf,       // .gltf plus a .bin buffer
  Glb,        // binary glTF, one file
};

enum class MeshQuality {
  AsDisplayed, // the meshes the viewer built at import
  Fine,        // a quarter of the display's chordal tolerance, 10 degrees
  ExtraFine,   // a tenth of the display's chordal tolerance, 5 degrees
  Custom,      // customTolerance (in 'unit') and customAngleDegrees
};

enum class UpAxis {
  Y,
  Z,
};

struct GeometryExportOptions {
  GeometryFormat format = GeometryFormat::StlMerged;
  MeshQuality quality = MeshQuality::AsDisplayed;
  // Unit of the coordinates written to STL and OBJ, and of customTolerance. glTF is always metres.
  LengthUnit unit = LengthUnit::Millimeter;
  double customTolerance = 0.01;
  double customAngleDegrees = 10.0;
  // OBJ only. STL keeps the model's Z up; glTF is always Y up.
  UpAxis objUpAxis = UpAxis::Y;
};

struct GeometryExportResult {
  bool ok = false;
  bool cancelled = false;
  std::string error;              // set when !ok && !cancelled
  std::vector<std::string> files; // UTF-8 paths written, the main file first
  int skippedFaces = 0;           // faces without a mesh, left out
};

// Whether a format writes a folder of files (the path is a directory) rather than one file.
bool writesFolder(GeometryFormat format);
// The file name extension, with the dot (".stl", ".glb"). StlPerPart gives ".stl".
std::string fileExtension(GeometryFormat format);
// The chordal tolerance and angle a preset uses for a part, in millimetres and radians.
// AsDisplayed gives the viewer's own values.
std::pair<double, double> meshParameters(const TopoDS_Shape& part, const GeometryExportOptions& options);

// A file name made from a part name: characters that Windows, macOS or Linux refuse become '_',
// leading and trailing dots and spaces go, and Windows' reserved names get a '_' appended.
std::string safeFileName(const std::string& name);
// The per-part STL file names for the given part names, in order: safe, with "-2", "-3", ...
// appended to repeats (compared ignoring ASCII case, as Windows and macOS do).
std::vector<std::string> perPartFileNames(const std::vector<std::string>& names);

// Writes the given part instances (assembly nodes are expanded to their parts). 'utf8Path' is the
// file to write, or for StlPerPart the folder to write into (created if needed). Files that cannot
// be completed (error or cancel) are removed.
//
// Blocking; run it off the UI thread. It reads the index's document and never modifies it: finer
// qualities mesh a private copy. Takes the importer's lock for OCCT's shared XCAF application.
GeometryExportResult exportGeometry(const AssemblyIndex& index, const std::vector<NodeId>& nodes,
                                    const std::string& utf8Path, const GeometryExportOptions& options,
                                    const ProgressCallback& progress = {},
                                    const CancelToken* cancel = nullptr);

} // namespace sc::core
