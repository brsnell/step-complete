#include "core/StepImporter.h"

#include "core/CompressedStep.h"
#include "core/OccCompat.h"
#include "core/OccHandle.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Check.hxx>
#include <Interface_CheckIterator.hxx>
#include <Interface_InterfaceModel.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressScope.hxx>
#include <OSD_Parallel.hxx>
#include <OSD_ThreadPool.hxx>
#include <Prs3d_Drawer.hxx>
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
#include <StepGeom_CartesianPoint.hxx>
#include <StepGeom_Direction.hxx>
#include <StepGeom_GeomRepContextAndGlobUnitAssCtxAndGlobUncertaintyAssCtx.hxx>
#include <StepGeom_GeometricRepresentationContextAndGlobalUnitAssignedContext.hxx>
#include <StepRepr_ReprItemAndMeasureWithUnit.hxx>
#include <StepVisual_CameraModelD3.hxx>
#include <StepVisual_PlanarBox.hxx>
#include <StepVisual_ViewVolume.hxx>
#include <TCollection_HAsciiString.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
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
  if (const auto si = OccHandle<StepBasic_SiUnit>::DownCast(unit); !si.IsNull()) {
    if (si->Name() != StepBasic_sunMetre) {
      return 0.0;
    }
    int exponent = 0; // power of ten relative to the metre
    if (si->HasPrefix()) {
      switch (si->Prefix()) {
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

// Bridges OCCT's progress reporting to our callback and cancel token.
class ProgressBridge : public Message_ProgressIndicator {
public:
  ProgressBridge(const ProgressCallback& callback, const CancelToken* cancel)
      : m_callback(callback), m_cancel(cancel) {}

  void setStage(std::string stage) {
    m_stage = std::move(stage);
    report(GetPosition(), true);
  }

  bool UserBreak() override { return m_cancel != nullptr && m_cancel->isCancelled(); }

  void Show(const Message_ProgressScope&, const bool isForce) override { report(GetPosition(), isForce); }

  void report(double fraction, bool force) {
    if (!m_callback) {
      return;
    }
    const Clock::time_point now = Clock::now();
    if (!force && now - m_lastReport < std::chrono::milliseconds(50)) {
      return;
    }
    m_lastReport = now;
    m_callback(fraction, m_stage);
  }

private:
  const ProgressCallback& m_callback;
  const CancelToken* m_cancel;
  std::string m_stage;
  Clock::time_point m_lastReport{};
};

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
    m_path = std::filesystem::temp_directory_path() / name;
  }
  ~TempFile() {
    std::error_code ec;
    std::filesystem::remove(m_path, ec);
  }
  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;

  std::string utf8() const { return m_path.u8string(); }

private:
  std::filesystem::path m_path;
};

} // namespace

ImportResult importStep(const std::string& utf8Path, const ImportOptions& options,
                        const ProgressCallback& progress, const CancelToken* cancel) {
  ImportResult result;
  std::lock_guard<std::mutex> lock(g_importMutex);

  try {
    auto document = std::make_shared<Document>();
    document->setSourcePath(utf8Path);

    OccHandle<ProgressBridge> bridge = new ProgressBridge(progress, cancel);
    // Gzip / zip containers (.stpz, .stp.gz) are inflated to a temporary file first: OCCT 7.6 has
    // no stream reader. Detection is by content, so misnamed files work.
    const Compression compression = detectCompression(utf8Path);
    constexpr double kDecompressShare = 10.0;
    Message_ProgressScope root(bridge->Start(), "Import",
                               compression == Compression::None ? 100.0 : 100.0 + kDecompressShare);

    std::optional<TempFile> temp;
    std::string readPath = utf8Path;
    if (compression != Compression::None) {
      bridge->setStage("Decompressing");
      DecompressOptions decompress;
      decompress.isCancelled = [cancel] { return cancelled(cancel); };
      decompress.progress = [&bridge](double fraction) {
        bridge->report(fraction * kDecompressShare / (100.0 + kDecompressShare), false);
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
      root.Next(kDecompressShare);
    }

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
      result.error = status == IFSelect_RetVoid ? "The file is empty or contains no STEP data."
                                                : "The file could not be read as STEP.";
      return result;
    }
    if (cancelled(cancel)) {
      result.cancelled = true;
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

    // --- Pre-mesh unique parts -------------------------------------------------
    OccHandle<XCAFDoc_ShapeTool> shapeTool = XCAFDoc_DocumentTool::ShapeTool(ocaf->Main());
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
  } catch (const Standard_Failure& failure) {
    result.error = "Open CASCADE error: " + occ::errorMessage(failure);
  } catch (const std::exception& error) {
    result.error = std::string("Error: ") + error.what();
  }
  return result;
}

} // namespace sc::core
