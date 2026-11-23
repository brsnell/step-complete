#include "core/StepImporter.h"

#include "core/CompressedStep.h"
#include "core/DocumentCache.h"
#include "core/FileFormat.h"
#include "core/OccCompat.h"
#include "core/OccHandle.h"
#include "core/ProgressBridge.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IGESCAFControl_Reader.hxx>
#include <IGESData_GlobalSection.hxx>
#include <IGESData_IGESModel.hxx>
#include <Interface_Check.hxx>
#include <Interface_CheckIterator.hxx>
#include <Interface_EntityIterator.hxx>
#include <Interface_Graph.hxx>
#include <Interface_InterfaceModel.hxx>
#include <Message_ProgressScope.hxx>
#include <OSD_Parallel.hxx>
#include <OSD_ThreadPool.hxx>
#include <Prs3d_Drawer.hxx>
#include <RWGltf_CafReader.hxx>
#include <RWMesh_CoordinateSystem.hxx>
#include <RWObj_CafReader.hxx>
#include <RWStl.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <StdPrs_ToolTriangulatedShape.hxx>
#include <StepBasic_ConversionBasedUnit.hxx>
#include <StepBasic_MeasureWithUnit.hxx>
#include <StepBasic_NamedUnit.hxx>
#include <StepBasic_SiPrefix.hxx>
#include <StepBasic_SiUnit.hxx>
#include <StepBasic_SiUnitName.hxx>
#include <StepBasic_Unit.hxx>
#include <StepGeom_Axis2Placement3d.hxx>
#include <StepGeom_BSplineCurve.hxx>
#include <StepGeom_BSplineSurface.hxx>
#include <StepGeom_CartesianPoint.hxx>
#include <StepGeom_Direction.hxx>
#include <StepGeom_GeomRepContextAndGlobUnitAssCtxAndGlobUncertaintyAssCtx.hxx>
#include <StepGeom_GeometricRepresentationContextAndGlobalUnitAssignedContext.hxx>
#include <StepGeom_HArray1OfCartesianPoint.hxx>
#include <StepGeom_HArray2OfCartesianPoint.hxx>
#include <StepGeom_Pcurve.hxx>
#include <StepRepr_ReprItemAndMeasureWithUnit.hxx>
#include <StepShape_FaceSurface.hxx>
#include <StepVisual_CameraModelD3.hxx>
#include <StepVisual_PlanarBox.hxx>
#include <StepVisual_ViewVolume.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TDF_ChildIterator.hxx>
#include <TDataStd_Name.hxx>
#include <TopoDS_Face.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_VisMaterial.hxx>
#include <XCAFDoc_VisMaterialTool.hxx>
#include <XSControl_WorkSession.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_XYZ.hxx>

#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <unordered_set>

namespace sc::core {

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

// Direction/point coordinates from STEP entities; false for malformed ones.
bool stepVector(const OccHandle<StepGeom_Direction>& direction, gp_Dir& out) {
  if (direction.IsNull() || direction->NbDirectionRatios() < 3) {
    return false;
  }
  const gp_XYZ ratios(direction->DirectionRatiosValue(1), direction->DirectionRatiosValue(2),
                      direction->DirectionRatiosValue(3));
  if (ratios.Modulus() < 1e-12) {
    return false;
  }
  out = gp_Dir(ratios);
  return true;
}

gp_Pnt stepPoint(const OccHandle<StepGeom_CartesianPoint>& point, double mmPerUnit) {
  if (point.IsNull() || point->NbCoordinates() < 3) {
    return gp_Pnt();
  }
  return gp_Pnt(point->CoordinatesValue(1) * mmPerUnit, point->CoordinatesValue(2) * mmPerUnit,
                point->CoordinatesValue(3) * mmPerUnit);
}

// AXIS2_PLACEMENT_3D frame; the reference direction is projected onto the plane normal to the axis.
gp_Ax2 placementFrame(const gp_Pnt& origin, const gp_Dir& axis, const gp_Dir& ref) {
  return ref.IsParallel(axis, 1e-9) ? gp_Ax2(origin, axis) : gp_Ax2(origin, axis, ref);
}

// Millimeters per unit of a LENGTH_UNIT: an SI unit (with prefix) or a conversion-based unit
// (inch, foot, ...) defined as a multiple of an SI unit. Zero if it cannot be determined.
double lengthUnitInMillimeters(const OccHandle<StepBasic_NamedUnit>& unit, int depth = 0) {
  if (unit.IsNull() || depth > 3) {
    return 0.0;
  }
  if (const auto siUnit = OccHandle<StepBasic_SiUnit>::DownCast(unit); !siUnit.IsNull()) {
    if (siUnit->Name() != StepBasic_sunMetre) {
      return 0.0;
    }
    int exponent = 0; // power of ten relative to the metre
    if (siUnit->HasPrefix()) {
      switch (siUnit->Prefix()) {
      case StepBasic_spKilo:
        exponent = 3;
        break;
      case StepBasic_spHecto:
        exponent = 2;
        break;
      case StepBasic_spDeca:
        exponent = 1;
        break;
      case StepBasic_spDeci:
        exponent = -1;
        break;
      case StepBasic_spCenti:
        exponent = -2;
        break;
      case StepBasic_spMilli:
        exponent = -3;
        break;
      case StepBasic_spMicro:
        exponent = -6;
        break;
      case StepBasic_spNano:
        exponent = -9;
        break;
      default:
        return 0.0;
      }
    }
    return 1000.0 * std::pow(10.0, exponent);
  }
  if (const auto converted = OccHandle<StepBasic_ConversionBasedUnit>::DownCast(unit); !converted.IsNull()) {
    // OCCT 8 returns the factor as Standard_Transient: a measure, or a complex
    // representation item that wraps one.
    OccHandle<StepBasic_MeasureWithUnit> factor = OccHandle<StepBasic_MeasureWithUnit>::DownCast(
        OccHandle<Standard_Transient>(converted->ConversionFactor()));
    if (factor.IsNull()) {
      if (const auto item = OccHandle<StepRepr_ReprItemAndMeasureWithUnit>::DownCast(
              OccHandle<Standard_Transient>(converted->ConversionFactor()));
          !item.IsNull()) {
        factor = item->GetMeasureWithUnit();
      }
    }
    if (factor.IsNull()) {
      return 0.0;
    }
    return factor->ValueComponent() * lengthUnitInMillimeters(factor->UnitComponent().NamedUnit(), depth + 1);
  }
  return 0.0;
}

// Millimeters per length unit of the file's geometric representation context, the factor OCCT
// scales the geometry by. Zero if there is no such context. (STEPControl_Reader::FileUnits
// misses contexts that also carry a global uncertainty, which is how most CAD systems write
// them.)
double geometryUnitInMillimeters(const OccHandle<Interface_InterfaceModel>& model) {
  const auto lengthOf = [](const auto& context) {
    for (int i = 1; i <= context->NbUnits(); ++i) {
      if (const double scale = lengthUnitInMillimeters(context->UnitsValue(i)); scale > 0.0) {
        return scale;
      }
    }
    return 0.0;
  };
  for (int i = 1; i <= model->NbEntities(); ++i) {
    const OccHandle<Standard_Transient> entity = model->Value(i);
    double scale = 0.0;
    if (const auto plain =
            OccHandle<StepGeom_GeometricRepresentationContextAndGlobalUnitAssignedContext>::DownCast(entity);
        !plain.IsNull()) {
      scale = lengthOf(plain);
    } else if (const auto withUncertainty =
                   OccHandle<StepGeom_GeomRepContextAndGlobUnitAssCtxAndGlobUncertaintyAssCtx>::DownCast(
                       entity);
               !withUncertainty.IsNull()) {
      scale = lengthOf(withUncertainty);
    }
    if (scale > 0.0) {
      return scale;
    }
  }
  return 0.0;
}

// Collects the CAMERA_MODEL_D3 entities (saved views) in file order. OCCT's
// XCAF view object drops the camera location and keeps the view window in file
// units, so both are read here. Lengths are converted with the factor OCCT
// applied to the geometry.
std::vector<StepCamera> readCameras(const OccHandle<Interface_InterfaceModel>& model, double fallbackScale) {
  std::vector<StepCamera> cameras;
  if (model.IsNull()) {
    return cameras;
  }
  const double unitScale = geometryUnitInMillimeters(model);
  const double mmPerUnit = unitScale > 0.0 ? unitScale : fallbackScale;
  for (int i = 1; i <= model->NbEntities(); ++i) {
    const OccHandle<StepVisual_CameraModelD3> camera =
        OccHandle<StepVisual_CameraModelD3>::DownCast(model->Value(i));
    if (camera.IsNull()) {
      continue;
    }
    const OccHandle<StepGeom_Axis2Placement3d> frame = camera->ViewReferenceSystem();
    const OccHandle<StepVisual_ViewVolume> volume = camera->PerspectiveOfVolume();
    if (frame.IsNull() || volume.IsNull()) {
      continue;
    }
    StepCamera out;
    if (!camera->Name().IsNull()) {
      out.name = camera->Name()->ToCString();
    }
    out.location = stepPoint(frame->Location(), mmPerUnit);
    if (frame->HasAxis() && !stepVector(frame->Axis(), out.axis)) {
      continue;
    }
    gp_Dir ref(1.0, 0.0, 0.0);
    if (frame->HasRefDirection()) {
      stepVector(frame->RefDirection(), ref);
    }
    out.refDirection = placementFrame(out.location, out.axis, ref).XDirection();

    out.perspective = volume->ProjectionType() == StepVisual_copCentral;
    out.projectionPoint = stepPoint(volume->ProjectionPoint(), mmPerUnit);
    out.viewPlaneDistance = volume->ViewPlaneDistance() * mmPerUnit;
    out.frontPlaneClipping = volume->FrontPlaneClipping();
    out.backPlaneClipping = volume->BackPlaneClipping();
    out.sidesClipping = volume->ViewVolumeSidesClipping();
    if (out.frontPlaneClipping) {
      out.frontPlaneDistance = volume->FrontPlaneDistance() * mmPerUnit;
    }
    if (out.backPlaneClipping) {
      out.backPlaneDistance = volume->BackPlaneDistance() * mmPerUnit;
    }

    const OccHandle<StepVisual_PlanarBox> window = volume->ViewWindow();
    if (!window.IsNull()) {
      out.windowWidth = window->SizeInX() * mmPerUnit;
      out.windowHeight = window->SizeInY() * mmPerUnit;
      // The box placement is its lower-left corner and axes in the camera frame.
      const OccHandle<StepGeom_Axis2Placement3d> placement = window->Placement().Axis2Placement3d();
      if (!placement.IsNull()) {
        const gp_Pnt corner = stepPoint(placement->Location(), mmPerUnit);
        gp_Dir boxZ(0.0, 0.0, 1.0);
        gp_Dir boxX(1.0, 0.0, 0.0);
        if (placement->HasAxis()) {
          stepVector(placement->Axis(), boxZ);
        }
        if (placement->HasRefDirection()) {
          stepVector(placement->RefDirection(), boxX);
        }
        const gp_Ax2 box = placementFrame(corner, boxZ, boxX);
        const gp_Vec center = gp_Vec(corner.XYZ()) + gp_Vec(box.XDirection()) * (out.windowWidth * 0.5) +
                              gp_Vec(box.YDirection()) * (out.windowHeight * 0.5);
        out.windowCenterX = center.X();
        out.windowCenterY = center.Y();
      }
    }
    cameras.push_back(std::move(out));
  }
  return cameras;
}

// OCCT translators keep their parameters in process-global state.
std::mutex g_importMutex;

void collectChecks(const Interface_CheckIterator& checks, std::vector<ImportMessage>& out) {
  std::map<std::pair<ImportMessage::Severity, std::string>, int> merged;
  Interface_CheckIterator it = checks;
  for (it.Start(); it.More(); it.Next()) {
    const OccHandle<Interface_Check>& check = it.Value();
    for (int i = 1; i <= check->NbFails(); ++i) {
      ++merged[{ImportMessage::Severity::Error, check->CFail(i)}];
    }
    for (int i = 1; i <= check->NbWarnings(); ++i) {
      ++merged[{ImportMessage::Severity::Warning, check->CWarning(i)}];
    }
  }
  for (const auto& [key, count] : merged) {
    out.push_back({key.first, key.second, count});
  }
}

template <class Spline>
bool hasNullControlPoint(const OccHandle<Spline>& spline);

template <>
bool hasNullControlPoint(const OccHandle<StepGeom_BSplineSurface>& surface) {
  const auto points = surface->ControlPointsList();
  if (points.IsNull()) {
    return true;
  }
  for (int i = points->LowerRow(); i <= points->UpperRow(); ++i) {
    for (int j = points->LowerCol(); j <= points->UpperCol(); ++j) {
      if (points->Value(i, j).IsNull()) {
        return true;
      }
    }
  }
  return false;
}

template <>
bool hasNullControlPoint(const OccHandle<StepGeom_BSplineCurve>& curve) {
  const auto points = curve->ControlPointsList();
  if (points.IsNull()) {
    return true;
  }
  for (int i = points->Lower(); i <= points->Upper(); ++i) {
    if (points->Value(i).IsNull()) {
      return true;
    }
  }
  return false;
}

// The entities' labels as written in the file (#82013), at most five.
std::string entityList(const OccHandle<Interface_InterfaceModel>& model,
                       const std::vector<OccHandle<Standard_Transient>>& entities) {
  std::string out;
  for (size_t i = 0; i < entities.size() && i < 5; ++i) {
    const OccHandle<TCollection_HAsciiString> label = model->StringLabel(entities[i]);
    out += (i == 0 ? "" : ", ") + std::string(label.IsNull() ? "?" : label->ToCString());
  }
  return entities.size() > 5 ? out + ", ..." : out;
}

// Some exporters (seen from ST-DEVELOPER v20) write B-spline control points that
// reference other entity types (LINE, VECTOR). The parser leaves those points null
// and OCCT's translator dereferences them without a check, crashing the import.
// A face on such a surface gets an empty placeholder surface instead, which the
// translator rejects cleanly, so the rest of the model still imports. Any other use
// of a broken spline (pcurves aside) cannot be skipped safely: the returned error
// rejects the file.
std::string neutralizeBrokenSplines(const OccHandle<Interface_InterfaceModel>& model,
                                    std::vector<ImportMessage>& messages) {
  std::vector<OccHandle<Standard_Transient>> broken;
  for (int i = 1; i <= model->NbEntities(); ++i) {
    const OccHandle<Standard_Transient>& entity = model->Value(i);
    const auto surface = OccHandle<StepGeom_BSplineSurface>::DownCast(entity);
    const auto curve = OccHandle<StepGeom_BSplineCurve>::DownCast(entity);
    const bool brokenSurface = !surface.IsNull() && hasNullControlPoint(surface);
    const bool brokenCurve = !curve.IsNull() && hasNullControlPoint(curve);
    if (brokenSurface || brokenCurve) {
      broken.push_back(entity);
    }
  }
  if (broken.empty()) {
    return {};
  }

  const Interface_Graph graph(model);
  std::vector<OccHandle<Standard_Transient>> skipped, unrecoverable;
  for (const OccHandle<Standard_Transient>& spline : broken) {
    std::vector<OccHandle<StepShape_FaceSurface>> faces;
    bool onlyFaces = !OccHandle<StepGeom_Surface>::DownCast(spline).IsNull();
    for (Interface_EntityIterator users = graph.Sharings(spline); onlyFaces && users.More(); users.Next()) {
      // Pcurves are only read for the face whose surface they lie on, and the
      // placeholder matches none of them, so they are left alone.
      if (!OccHandle<StepGeom_Pcurve>::DownCast(users.Value()).IsNull()) {
        continue;
      }
      const auto face = OccHandle<StepShape_FaceSurface>::DownCast(users.Value());
      if (face.IsNull() || face->FaceGeometry() != spline) {
        onlyFaces = false;
      } else {
        faces.push_back(face);
      }
    }
    if (!onlyFaces) {
      unrecoverable.push_back(spline);
      continue;
    }
    for (const auto& face : faces) {
      OccHandle<StepGeom_Surface> placeholder = new StepGeom_Surface();
      placeholder->Init(OccHandle<StepGeom_Surface>::DownCast(spline)->Name());
      face->SetFaceGeometry(placeholder);
    }
    skipped.push_back(spline);
  }

  if (!unrecoverable.empty()) {
    std::string error = "The file is malformed: ";
    error += std::to_string(unrecoverable.size());
    error += " B-spline curve(s) or surface(s) have invalid control points (";
    error += entityList(model, unrecoverable);
    error += "). The exporting program wrote references to other entity types where points belong.";
    return error;
  }
  std::string warning = "Skipped the faces of ";
  warning += std::to_string(skipped.size());
  warning += " B-spline surface(s) with invalid control points (";
  warning += entityList(model, skipped);
  warning += "); the file is malformed.";
  messages.push_back({ImportMessage::Severity::Warning, std::move(warning)});
  return {};
}

bool cancelled(const CancelToken* cancel) {
  return cancel != nullptr && cancel->isCancelled();
}

// A uniquely named temporary file that is removed when the guard goes away.
class TempFile {
public:
  TempFile() {
    static std::atomic<unsigned> counter{0};
    std::random_device random;
    const std::string name =
        "stepcomplete-" + std::to_string(random()) + "-" + std::to_string(counter.fetch_add(1)) + ".stp";
    _path = std::filesystem::temp_directory_path() / name;
  }
  ~TempFile() {
    std::error_code ec;
    std::filesystem::remove(_path, ec);
  }
  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;

  std::string utf8() const { return _path.u8string(); }

private:
  std::filesystem::path _path;
};

// Cached documents differ in content with the PMI option, so it is part of the key.
std::string cacheKeyFor(const std::string& utf8Path, const ImportOptions& options) {
  if (options.cacheDir.empty() || !options.premesh) {
    return {};
  }
  return cacheKey(utf8Path, options.readPmi ? "pmi=1" : "pmi=0");
}

bool storeLocked(const std::string& key, const ImportResult& result, const ImportOptions& options) {
  if (key.empty() || !result.ok || result.stats.fromCache) {
    return false;
  }
  return DocumentCache(std::filesystem::u8path(options.cacheDir))
      .store(key, *result.document, result.messages, result.stats.rootCount, result.stats.uniqueParts);
}

// The format of a file as stored (a compressed file by its name); STEP when unknown.
FileFormat formatOf(const std::string& utf8Path) {
  const FileFormat format = detectFileFormat(utf8Path);
  return format == FileFormat::Unknown ? FileFormat::Step : format;
}

// Reads an IGES file into the document: names, colors, layers and the file unit. IGES has no
// assembly structure, PMI or saved views: each top-level entity becomes a part. Fills the
// result's error, cancelled flag, messages and stats; false when the import must stop.
bool readIges(const std::string& readPath, Document& document, Message_ProgressScope& root,
              ProgressBridge& bridge, const CancelToken* cancel, ImportResult& result) {
  bridge.setStage("Reading file");
  const Clock::time_point readStart = Clock::now();
  IGESCAFControl_Reader reader;
  reader.SetNameMode(true);
  reader.SetColorMode(true);
  reader.SetLayerMode(true);
  const IFSelect_ReturnStatus status = reader.ReadFile(readPath.c_str());
  result.stats.readSeconds = secondsSince(readStart);
  collectChecks(reader.WS()->ModelCheckList(), result.messages);
  if (status != IFSelect_RetDone) {
    result.error = status == IFSelect_RetVoid ? "The file is empty or contains no IGES data."
                                              : "The file could not be read as IGES.";
    return false;
  }
  if (cancelled(cancel)) {
    result.cancelled = true;
    return false;
  }
  root.Next(10.0); // parsing reports no progress of its own

  bridge.setStage("Building model");
  const Clock::time_point transferStart = Clock::now();
  result.stats.rootCount = reader.NbRootsForTransfer();
  OccHandle<TDocStd_Document> ocaf = document.ocaf();
  const bool transferred = reader.Transfer(ocaf, root.Next(65.0));
  result.stats.transferSeconds = secondsSince(transferStart);
  if (cancelled(cancel) || !root.More()) {
    result.cancelled = cancelled(cancel);
    if (!result.cancelled) {
      result.error = "Import was interrupted.";
    }
    return false;
  }
  if (!transferred) {
    result.error = "The file contains no geometry that could be translated.";
    return false;
  }

  // The unit of the global section; OCCT has already converted the geometry to millimetres.
  const OccHandle<IGESData_IGESModel> model = OccHandle<IGESData_IGESModel>::DownCast(reader.WS()->Model());
  if (!model.IsNull()) {
    const IGESData_GlobalSection& global = model->GlobalSection();
    const OccHandle<TCollection_HAsciiString> unitName = global.UnitName();
    std::string name = unitName.IsNull() ? std::string() : std::string(unitName->ToCString());
    std::optional<LengthUnit> unit = lengthUnitFromIgesFlag(global.UnitFlag());
    if (!unit && !name.empty()) {
      unit = lengthUnitFromStepName(name); // flag 3: the unit is named
    }
    document.setFileUnit(std::move(name), unit);
  }
  return true;
}

// OBJ and glTF colors arrive as visual materials, which the viewer would draw with physically
// based settings that wash them out, and which the assembly tree does not read. Each becomes the
// plain surface color (with its alpha) that STEP and IGES parts carry, and the material goes.
void materialsToColors(const OccHandle<TDocStd_Document>& ocaf) {
  const OccHandle<XCAFDoc_VisMaterialTool> materials = XCAFDoc_DocumentTool::VisMaterialTool(ocaf->Main());
  const OccHandle<XCAFDoc_ColorTool> colors = XCAFDoc_DocumentTool::ColorTool(ocaf->Main());
  const OccHandle<XCAFDoc_ShapeTool> shapes = XCAFDoc_DocumentTool::ShapeTool(ocaf->Main());
  std::vector<TDF_Label> labels;
  for (TDF_ChildIterator it(shapes->Label(), true); it.More(); it.Next()) {
    labels.push_back(it.Value());
  }
  for (const TDF_Label& label : labels) {
    const OccHandle<XCAFDoc_VisMaterial> material = materials->GetShapeMaterial(label);
    if (material.IsNull()) {
      continue;
    }
    colors->SetColor(label, material->BaseColor(), XCAFDoc_ColorSurf);
    materials->UnSetShapeMaterial(label);
  }
}

// Reads a mesh file (docs/designs/mesh-import.md): STL into one part named after the file, OBJ
// and glTF through OCCT's XCAF mesh readers (names, colors, the glTF hierarchy). Every mesh is a
// face that holds only a triangulation. glTF is metres and Y up; OBJ is read as Y up; STL and OBJ
// have no unit, so millimetres are assumed. Fills the result like readIges.
bool readMesh(const std::string& readPath, const std::string& displayPath, FileFormat format,
              Document& document, Message_ProgressScope& root, ProgressBridge& bridge,
              const CancelToken* cancel, ImportResult& result) {
  bridge.setStage("Reading file");
  const Clock::time_point readStart = Clock::now();
  OccHandle<TDocStd_Document> ocaf = document.ocaf();
  if (format == FileFormat::Stl) {
    const OccHandle<Poly_Triangulation> mesh = RWStl::ReadFile(readPath.c_str(), root.Next(75.0));
    result.stats.readSeconds = secondsSince(readStart);
    if (cancelled(cancel)) {
      result.cancelled = true;
      return false;
    }
    if (mesh.IsNull() || mesh->NbTriangles() == 0) {
      result.error = "The file could not be read as STL, or it has no triangles.";
      return false;
    }
    TopoDS_Face face;
    BRep_Builder().MakeFace(face, mesh);
    const OccHandle<XCAFDoc_ShapeTool> shapes = XCAFDoc_DocumentTool::ShapeTool(ocaf->Main());
    const TDF_Label part = shapes->AddShape(face, false);
    const std::string stem = std::filesystem::u8path(displayPath).stem().u8string();
    TDataStd_Name::Set(part, TCollection_ExtendedString(stem.c_str(), true));
    result.stats.rootCount = 1;
  } else {
    std::unique_ptr<RWMesh_CafReader> reader;
    if (format == FileFormat::Gltf) {
      auto gltf = std::make_unique<RWGltf_CafReader>();
      gltf->SetParallel(true);
      gltf->SetFileLengthUnit(1.0); // metres, by the specification
      gltf->SetFileCoordinateSystem(RWMesh_CoordinateSystem_glTF);
      reader = std::move(gltf);
    } else {
      reader = std::make_unique<RWObj_CafReader>();
      reader->SetFileLengthUnit(0.001); // no unit in the file: millimetres assumed
      reader->SetFileCoordinateSystem(RWMesh_CoordinateSystem_Yup);
    }
    reader->SetSystemLengthUnit(0.001); // the document is in millimetres
    reader->SetSystemCoordinateSystem(RWMesh_CoordinateSystem_Zup);
    reader->SetDocument(ocaf);
    const bool read = reader->Perform(TCollection_AsciiString(readPath.c_str()), root.Next(75.0));
    result.stats.readSeconds = secondsSince(readStart);
    if (cancelled(cancel)) {
      result.cancelled = true;
      return false;
    }
    occ::LabelSequence roots;
    XCAFDoc_DocumentTool::ShapeTool(ocaf->Main())->GetFreeShapes(roots);
    if (!read || roots.IsEmpty()) {
      result.error = std::string("The file could not be read as ") + displayName(format) + ".";
      return false;
    }
    result.stats.rootCount = roots.Length();
    materialsToColors(ocaf);
  }
  if (format == FileFormat::Gltf) {
    document.setFileUnit("m", LengthUnit::Meter);
  } else {
    document.setFileUnit("mm (assumed)", std::nullopt); // no unit in STL or OBJ
  }
  return true;
}

} // namespace

ImportResult importModel(const std::string& utf8Path, const ImportOptions& options,
                         const ProgressCallback& progress, const CancelToken* cancel) {
  ImportResult result;
  std::lock_guard<std::mutex> lock(g_importMutex);

  try {
    // --- Open-file cache ---------------------------------------------------------
    const std::string cacheId = cacheKeyFor(utf8Path, options);
    if (!cacheId.empty()) {
      const Clock::time_point loadStart = Clock::now();
      if (auto cached = DocumentCache(std::filesystem::u8path(options.cacheDir)).load(cacheId)) {
        cached->document->setSourcePath(utf8Path);
        cached->document->setFormat(formatOf(utf8Path));
        result.document = std::move(cached->document);
        result.messages = std::move(cached->messages);
        result.messages.push_back({ImportMessage::Severity::Info, "Loaded from cache", 1});
        result.stats.rootCount = cached->rootCount;
        result.stats.uniqueParts = cached->uniqueParts;
        result.stats.fromCache = true;
        result.stats.cacheLoadSeconds = secondsSince(loadStart);
        result.ok = true;
        if (progress) {
          progress(1.0, "Loaded from cache");
        }
        return result;
      }
    }

    auto document = std::make_shared<Document>();
    document->setSourcePath(utf8Path);

    OccHandle<ProgressBridge> bridge = new ProgressBridge(progress, cancel);
    // Gzip / zip containers (.stpz, .stp.gz) are inflated to a temporary file first: OCCT 7.6 has
    // no stream reader. Detection is by content, so misnamed files work.
    const Compression compression = detectCompression(utf8Path);
    constexpr double DecompressShare = 10.0;
    Message_ProgressScope root(bridge->Start(), "Import",
                               compression == Compression::None ? 100.0 : 100.0 + DecompressShare);

    std::optional<TempFile> temp;
    std::string readPath = utf8Path;
    if (compression != Compression::None) {
      bridge->setStage("Decompressing");
      DecompressOptions decompress;
      decompress.isCancelled = [cancel] { return cancelled(cancel); };
      decompress.progress = [&bridge](double fraction) {
        bridge->report(fraction * DecompressShare / (100.0 + DecompressShare), false);
      };
      temp.emplace();
      const DecompressResult inflated = decompressStepFile(utf8Path, compression, temp->utf8(), decompress);
      if (inflated.cancelled) {
        result.cancelled = true;
        return result;
      }
      if (!inflated.ok) {
        result.error = inflated.error;
        return result;
      }
      readPath = temp->utf8();
      root.Next(DecompressShare);
    }

    // The format from the content (a .stp holding IGES opens as IGES); a compressed file's from
    // its inflated content and its own name.
    const FileFormat format = detectFileFormat(readPath, utf8Path);
    document->setFormat(format == FileFormat::Unknown ? FileFormat::Step : format);
    if (format == FileFormat::Iges) {
      if (!readIges(readPath, *document, root, *bridge, cancel, result)) {
        return result;
      }
      temp.reset();
    } else if (isMeshFormat(format)) {
      // OBJ and glTF name their side files (.mtl, .bin, textures) relative to the file: a
      // compressed one is read from its inflated copy, which has none of them.
      if (!readMesh(readPath, utf8Path, format, *document, root, *bridge, cancel, result)) {
        return result;
      }
      temp.reset();
    } else {
      // --- Parse ---------------------------------------------------------------
      bridge->setStage("Reading file");
      const Clock::time_point readStart = Clock::now();
      STEPCAFControl_Reader reader;
      reader.SetNameMode(true);
      reader.SetColorMode(true);
      reader.SetLayerMode(true);
      reader.SetPropsMode(true);
      reader.SetSHUOMode(true);
      reader.SetMatMode(true);
      reader.SetGDTMode(options.readPmi);
      reader.SetViewMode(options.readPmi);
#if OCC_VERSION_HEX >= 0x070700
      reader.SetMetaMode(true);
#endif

      // OCCT handles UTF-8 paths on all platforms (wide-char APIs on Windows).
      const IFSelect_ReturnStatus status = reader.ReadFile(readPath.c_str());
      temp.reset();
      result.stats.readSeconds = secondsSince(readStart);
      collectChecks(reader.Reader().WS()->ModelCheckList(), result.messages);
      if (status != IFSelect_RetDone) {
        if (format == FileFormat::Unknown) {
          result.error = "The file is not a STEP or IGES file.";
        } else {
          result.error = status == IFSelect_RetVoid ? "The file is empty or contains no STEP data."
                                                    : "The file could not be read as STEP.";
        }
        return result;
      }
      if (cancelled(cancel)) {
        result.cancelled = true;
        return result;
      }
      if (std::string broken = neutralizeBrokenSplines(reader.Reader().Model(), result.messages);
          !broken.empty()) {
        result.error = std::move(broken);
        return result;
      }
      root.Next(10.0); // parsing reports no progress of its own

      // --- Transfer to XCAF ----------------------------------------------------
      bridge->setStage("Building model");
      const Clock::time_point transferStart = Clock::now();
      result.stats.rootCount = reader.NbRootsForTransfer();
      OccHandle<TDocStd_Document> ocaf = document->ocaf();
      const bool transferred = reader.Transfer(ocaf, root.Next(65.0));
      result.stats.transferSeconds = secondsSince(transferStart);
      if (cancelled(cancel) || !root.More()) {
        result.cancelled = cancelled(cancel);
        if (!result.cancelled) {
          result.error = "Import was interrupted.";
        }
        return result;
      }
      if (!transferred) {
        result.error = "The file contains no geometry that could be translated.";
        return result;
      }

      occ::AsciiStringSequence lengthUnits, angleUnits, solidAngleUnits;
      reader.ChangeReader().FileUnits(lengthUnits, angleUnits, solidAngleUnits);
      if (!lengthUnits.IsEmpty()) {
        const std::string unitName = lengthUnits.First().ToCString();
        document->setFileUnit(unitName, lengthUnitFromStepName(unitName));
      }

      // Saved-view cameras: lengths in the file's unit, converted like the geometry.
      const std::optional<LengthUnit> fileUnit = document->fileUnit();
      document->setCameras(readCameras(reader.Reader().Model(), fileUnit ? millimetersPer(*fileUnit) : 1.0));
    }

    // --- Pre-mesh unique parts -------------------------------------------------
    OccHandle<XCAFDoc_ShapeTool> shapeTool = XCAFDoc_DocumentTool::ShapeTool(document->ocaf()->Main());
    occ::LabelSequence shapeLabels;
    shapeTool->GetShapes(shapeLabels);
    std::vector<TopoDS_Shape> parts;
    std::unordered_set<const void*> seenShapes; // mesh each TShape once, never concurrently
    for (const TDF_Label& label : shapeLabels) {
      if (!XCAFDoc_ShapeTool::IsAssembly(label)) {
        const TopoDS_Shape part = XCAFDoc_ShapeTool::GetShape(label);
        if (!part.IsNull() && seenShapes.insert(part.TShape().get()).second) {
          parts.push_back(part);
        }
      }
    }
    result.stats.uniqueParts = static_cast<int>(parts.size());

    if (options.premesh && !parts.empty()) {
      bridge->setStage("Meshing");
      const Clock::time_point meshStart = Clock::now();
      Message_ProgressScope meshScope(root.Next(25.0), "Meshing", static_cast<double>(parts.size()));
      // Progress ranges must be created in order on this thread; the indicator
      // itself is thread-safe, so each worker reports through its own range.
      std::vector<Message_ProgressRange> ranges;
      ranges.reserve(parts.size());
      for (size_t i = 0; i < parts.size(); ++i) {
        ranges.push_back(meshScope.Next());
      }
      // Many parts: mesh parts in parallel. Few big parts: parallelize over faces instead.
      const int threads = OSD_ThreadPool::DefaultPool()->NbThreads();
      const bool acrossParts = threads > 1 && parts.size() >= static_cast<size_t>(2 * threads);
      const auto meshPart = [&](int i) {
        Message_ProgressScope partScope(ranges[static_cast<size_t>(i)], nullptr, 1.0);
        if (cancelled(cancel) || !partScope.More()) {
          return;
        }
        // Same deflection as the viewer's default drawer, so the viewer reuses
        // these triangulations instead of meshing again. The drawer is per part:
        // computing a relative deflection writes into it.
        const TopoDS_Shape& part = parts[static_cast<size_t>(i)];
        OccHandle<Prs3d_Drawer> drawer = new Prs3d_Drawer();
        if (StdPrs_ToolTriangulatedShape::IsTessellated(part, drawer)) {
          return;
        }
        IMeshTools_Parameters parameters;
        parameters.Deflection = StdPrs_ToolTriangulatedShape::GetDeflection(part, drawer);
        parameters.Angle = drawer->DeviationAngle();
        parameters.InParallel = !acrossParts;
        BRepMesh_IncrementalMesh mesher(part, parameters);
      };
      OSD_Parallel::For(0, static_cast<int>(parts.size()), meshPart, !acrossParts);
      result.stats.meshSeconds = secondsSince(meshStart);
      if (cancelled(cancel)) {
        result.cancelled = true;
        return result;
      }
    }

    bridge->report(1.0, true);
    result.document = std::move(document);
    result.ok = true;

    if (!cacheId.empty() && !options.deferCacheStore) {
      const Clock::time_point storeStart = Clock::now();
      storeLocked(cacheId, result, options);
      result.stats.cacheStoreSeconds = secondsSince(storeStart);
    }
  } catch (const Standard_Failure& failure) {
    result.error = "Open CASCADE error: " + occ::errorMessage(failure);
  } catch (const std::exception& error) {
    result.error = std::string("Error: ") + error.what();
  }
  return result;
}

std::mutex& xcafMutex() {
  return g_importMutex;
}

bool storeInCache(const std::string& utf8Path, const ImportResult& result, const ImportOptions& options) {
  const std::string key = cacheKeyFor(utf8Path, options);
  if (key.empty() || !result.ok || result.stats.fromCache) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_importMutex);
  try {
    return storeLocked(key, result, options);
  } catch (const std::exception&) {
    return false;
  }
}

} // namespace sc::core
