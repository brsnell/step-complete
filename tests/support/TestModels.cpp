#include "TestModels.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <STEPControl_StepModelType.hxx>
#include <TDataStd_Name.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Trsf.hxx>

#include <atomic>
#include <filesystem>

namespace sc::test {

namespace {

TopLoc_Location translation(double x, double y, double z) {
  gp_Trsf trsf;
  trsf.SetTranslation(gp_Vec(x, y, z));
  return TopLoc_Location(trsf);
}

void setName(const TDF_Label& label, const char* utf8) {
  TDataStd_Name::Set(label, TCollection_ExtendedString(utf8, true));
}

} // namespace

core::DocumentPtr buildFixtureAssembly(bool withHiddenInstance) {
  auto document = std::make_shared<core::Document>();
  const TDF_Label main = document->ocaf()->Main();
  auto shapes = XCAFDoc_DocumentTool::ShapeTool(main);
  auto colors = XCAFDoc_DocumentTool::ColorTool(main);

  const TDF_Label plate = shapes->AddShape(BRepPrimAPI_MakeBox(100.0, 60.0, 10.0).Shape(), false);
  setName(plate, "Base plate");
  colors->SetColor(plate, Quantity_Color(Quantity_NOC_RED), XCAFDoc_ColorSurf);

  const TDF_Label pin = shapes->AddShape(BRepPrimAPI_MakeCylinder(3.0, 20.0).Shape(), false);
  setName(pin, "Pin \xC3\x98"
               "6"); // "Pin Ø6"

  const TDF_Label pair = shapes->NewShape();
  setName(pair, "Pin pair");
  shapes->AddComponent(pair, pin, TopLoc_Location());
  const TDF_Label secondPin = shapes->AddComponent(pair, pin, translation(20, 0, 0));
  colors->SetColor(secondPin, Quantity_Color(Quantity_NOC_GREEN), XCAFDoc_ColorSurf);

  const TDF_Label fixture = shapes->NewShape();
  setName(fixture, "Fixture");
  shapes->AddComponent(fixture, plate, TopLoc_Location());
  shapes->AddComponent(fixture, pair, translation(10, 10, 10));
  const TDF_Label secondPair = shapes->AddComponent(fixture, pair, translation(50, 10, 10));
  if (withHiddenInstance) {
    colors->SetVisibility(secondPair, false);
  }

  shapes->UpdateAssemblies();
  return document;
}

bool writeStep(const core::DocumentPtr& document, const std::string& utf8Path, const char* unit) {
  // Constructing the writer registers the STEP parameters; set them afterwards.
  STEPCAFControl_Writer writer;
  if (!Interface_Static::SetCVal("write.step.unit", unit)) {
    return false;
  }
  writer.SetNameMode(true);
  writer.SetColorMode(true);
  writer.SetLayerMode(true);
  OccHandle<TDocStd_Document> ocaf = document->ocaf();
  const bool transferred = writer.Transfer(ocaf, STEPControl_AsIs);
  const bool written = transferred && writer.Write(utf8Path.c_str()) == IFSelect_RetDone;
  Interface_Static::SetCVal("write.step.unit", "MM");
  return written;
}

std::string tempPath(const std::string& fileName) {
  static std::atomic<int> counter{0};
  const auto dir = std::filesystem::temp_directory_path() / "step-complete-tests";
  std::filesystem::create_directories(dir);
  return (dir / (std::to_string(counter++) + "-" + fileName)).string();
}

} // namespace sc::test
