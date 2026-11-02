#include "core/StepImporter.h"

#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Check.hxx>
#include <Interface_CheckIterator.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressScope.hxx>
#include <Prs3d_Drawer.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <StdPrs_ToolTriangulatedShape.hxx>
#include <TColStd_SequenceOfAsciiString.hxx>
#include <TDF_LabelSequence.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XSControl_WorkSession.hxx>

#include <chrono>
#include <exception>
#include <map>
#include <mutex>

namespace sc::core {

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
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

} // namespace

ImportResult importStep(const std::string& utf8Path, const ImportOptions& options,
                        const ProgressCallback& progress, const CancelToken* cancel) {
  ImportResult result;
  std::lock_guard<std::mutex> lock(g_importMutex);

  try {
    auto document = std::make_shared<Document>();
    document->setSourcePath(utf8Path);

    OccHandle<ProgressBridge> bridge = new ProgressBridge(progress, cancel);
    Message_ProgressScope root(bridge->Start(), "Import", 100.0);

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
    const IFSelect_ReturnStatus status = reader.ReadFile(utf8Path.c_str());
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

    TColStd_SequenceOfAsciiString lengthUnits, angleUnits, solidAngleUnits;
    reader.ChangeReader().FileUnits(lengthUnits, angleUnits, solidAngleUnits);
    if (!lengthUnits.IsEmpty()) {
      const std::string unitName = lengthUnits.First().ToCString();
      document->setFileUnit(unitName, lengthUnitFromStepName(unitName));
    }

    // --- Pre-mesh unique parts -------------------------------------------------
    OccHandle<XCAFDoc_ShapeTool> shapeTool = XCAFDoc_DocumentTool::ShapeTool(ocaf->Main());
    TDF_LabelSequence shapeLabels;
    shapeTool->GetShapes(shapeLabels);
    std::vector<TopoDS_Shape> parts;
    for (const TDF_Label& label : shapeLabels) {
      if (!XCAFDoc_ShapeTool::IsAssembly(label)) {
        parts.push_back(XCAFDoc_ShapeTool::GetShape(label));
      }
    }
    result.stats.uniqueParts = static_cast<int>(parts.size());

    if (options.premesh && !parts.empty()) {
      bridge->setStage("Meshing");
      const Clock::time_point meshStart = Clock::now();
      Message_ProgressScope meshScope(root.Next(25.0), "Meshing", static_cast<double>(parts.size()));
      for (const TopoDS_Shape& part : parts) {
        if (!meshScope.More()) {
          break;
        }
        if (!part.IsNull()) {
          // A default drawer matches the viewer's defaults, so the viewer
          // reuses these triangulations instead of meshing again.
          OccHandle<Prs3d_Drawer> drawer = new Prs3d_Drawer();
          StdPrs_ToolTriangulatedShape::Tessellate(part, drawer);
        }
        meshScope.Next();
      }
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
    result.error = std::string("Open CASCADE error: ") + failure.GetMessageString();
  } catch (const std::exception& error) {
    result.error = std::string("Error: ") + error.what();
  }
  return result;
}

} // namespace sc::core
