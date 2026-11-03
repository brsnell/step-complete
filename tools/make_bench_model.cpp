// Writes a synthetic STEP assembly for performance testing: N distinct parts
// (plates with holes and filleted edges), each placed once in a grid, plus
// instanced copies. Usage: sc_make_bench_model <out.stp> [parts=1000] [instances=2]
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

TopoDS_Shape makePart(int index) {
  const double w = 20.0 + (index % 17);
  const double d = 15.0 + (index % 11);
  const double h = 4.0 + (index % 5);
  TopoDS_Shape shape = BRepPrimAPI_MakeBox(w, d, h).Shape();

  BRepFilletAPI_MakeFillet fillet(shape);
  for (TopExp_Explorer it(shape, TopAbs_EDGE); it.More(); it.Next()) {
    fillet.Add(0.8, TopoDS::Edge(it.Current()));
  }
  fillet.Build();
  if (fillet.IsDone()) {
    shape = fillet.Shape();
  }

  const int holes = 2 + index % 5;
  for (int k = 0; k < holes; ++k) {
    const double x = w * (k + 1) / (holes + 1);
    const double y = d * (0.3 + 0.4 * ((k + index) % 2));
    const double r = 0.8 + 0.3 * ((index + k) % 4);
    const TopoDS_Shape hole =
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, y, -1.0), gp::DZ()), r, h + 2.0).Shape();
    BRepAlgoAPI_Cut cut(shape, hole);
    if (cut.IsDone()) {
      shape = cut.Shape();
    }
  }
  return shape;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s out.stp [parts=1000] [instances=2]\n", argv[0]);
    return 2;
  }
  const int parts = argc > 2 ? std::atoi(argv[2]) : 1000;
  const int instances = argc > 3 ? std::atoi(argv[3]) : 2;

  opencascade::handle<XCAFApp_Application> app = XCAFApp_Application::GetApplication();
  opencascade::handle<TDocStd_Document> doc;
  app->NewDocument("MDTV-XCAF", doc);
  opencascade::handle<XCAFDoc_ShapeTool> shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  opencascade::handle<XCAFDoc_ColorTool> colors = XCAFDoc_DocumentTool::ColorTool(doc->Main());

  const TDF_Label root = shapes->NewShape();
  const int columns = 32;
  for (int i = 0; i < parts; ++i) {
    const TDF_Label part = shapes->AddShape(makePart(i), false);
    colors->SetColor(
        part, Quantity_Color(0.3 + 0.6 * (i % 7) / 6.0, 0.5, 0.8 - 0.5 * (i % 5) / 4.0, Quantity_TOC_RGB),
        XCAFDoc_ColorSurf);
    for (int c = 0; c < instances; ++c) {
      gp_Trsf placement;
      placement.SetTranslation(gp_Vec(40.0 * (i % columns), 30.0 * (i / columns), 15.0 * c));
      shapes->AddComponent(root, part, TopLoc_Location(placement));
    }
    if (i % 100 == 99) {
      std::fprintf(stderr, "%d parts\n", i + 1);
    }
  }
  shapes->UpdateAssemblies();

  STEPCAFControl_Writer writer;
  writer.SetColorMode(true);
  writer.SetNameMode(true);
  if (!writer.Transfer(doc, STEPControl_AsIs) || writer.Write(argv[1]) != IFSelect_RetDone) {
    std::fprintf(stderr, "write failed\n");
    return 1;
  }
  app->Close(doc);
  return 0;
}
