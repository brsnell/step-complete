#include "PmiFixture.h"

#include "core/OccCompat.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TDataStd_Name.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFDimTolObjects_DatumObject.hxx>
#include <XCAFDimTolObjects_DimensionObject.hxx>
#include <XCAFDimTolObjects_GeomToleranceObject.hxx>
#include <XCAFDoc_ClippingPlaneTool.hxx>
#include <XCAFDoc_Datum.hxx>
#include <XCAFDoc_DimTolTool.hxx>
#include <XCAFDoc_Dimension.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_GeomTolerance.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_View.hxx>
#include <XCAFDoc_ViewTool.hxx>
#include <XCAFView_Object.hxx>

#include <initializer_list>

namespace sc::test {

namespace {

TDF_Label faceLabel(const OccHandle<XCAFDoc_ShapeTool>& shapes, const TDF_Label& part, const gp_Pnt& near) {
  const TopoDS_Shape solid = XCAFDoc_ShapeTool::GetShape(part);
  for (TopExp_Explorer it(solid, TopAbs_FACE); it.More(); it.Next()) {
    GProp_GProps props;
    BRepGProp::SurfaceProperties(it.Current(), props);
    if (props.CentreOfMass().Distance(near) < 1e-6) {
      return shapes->AddSubShape(part, it.Current());
    }
  }
  return {};
}

occ::LabelSequence labels(std::initializer_list<TDF_Label> items) {
  occ::LabelSequence sequence;
  for (const TDF_Label& item : items) {
    sequence.Append(item);
  }
  return sequence;
}

OccHandle<TCollection_HAsciiString> hs(const char* text) {
  return new TCollection_HAsciiString(text);
}

} // namespace

PmiFixture buildPmiFixture() {
  PmiFixture f;
  f.document = std::make_shared<core::Document>();
  const TDF_Label main = f.document->ocaf()->Main();
  auto shapes = XCAFDoc_DocumentTool::ShapeTool(main);
  auto gdt = XCAFDoc_DocumentTool::DimTolTool(main);

  f.part = shapes->AddShape(BRepPrimAPI_MakeBox(100.0, 60.0, 20.0).Shape(), false);
  TDataStd_Name::Set(f.part, "Block");
  f.top = faceLabel(shapes, f.part, gp_Pnt(50, 30, 20));
  f.bottom = faceLabel(shapes, f.part, gp_Pnt(50, 30, 0));
  f.side = faceLabel(shapes, f.part, gp_Pnt(100, 30, 10));

  // Datum A on the bottom face.
  f.datumA = gdt->AddDatum();
  OccHandle<XCAFDimTolObjects_DatumObject> datum = new XCAFDimTolObjects_DatumObject();
  datum->SetName(hs("A"));
  XCAFDoc_Datum::Set(f.datumA)->SetObject(datum);
  gdt->SetDatum(labels({f.bottom}), f.datumA);

  // Flatness on the top face.
  f.flatness = gdt->AddGeomTolerance();
  OccHandle<XCAFDimTolObjects_GeomToleranceObject> flat = new XCAFDimTolObjects_GeomToleranceObject();
  flat->SetType(XCAFDimTolObjects_GeomToleranceType_Flatness);
  flat->SetValue(0.05);
  XCAFDoc_GeomTolerance::Set(f.flatness)->SetObject(flat);
  gdt->SetGeomTolerance(f.top, f.flatness);

  // Parallelism to A, referencing a second datum label "A" like the STEP reader does.
  f.parallelism = gdt->AddGeomTolerance();
  OccHandle<XCAFDimTolObjects_GeomToleranceObject> parallel = new XCAFDimTolObjects_GeomToleranceObject();
  parallel->SetType(XCAFDimTolObjects_GeomToleranceType_Parallelism);
  parallel->SetValue(0.1);
  XCAFDoc_GeomTolerance::Set(f.parallelism)->SetObject(parallel);
  gdt->SetGeomTolerance(f.top, f.parallelism);
  f.datumARef = gdt->AddDatum();
  OccHandle<XCAFDimTolObjects_DatumObject> reference = new XCAFDimTolObjects_DatumObject();
  reference->SetName(hs("A"));
  reference->SetPosition(1);
  XCAFDoc_Datum::Set(f.datumARef)->SetObject(reference);
  gdt->SetDatumToGeomTol(f.datumARef, f.parallelism);

  // Height between top and bottom, with plane, text point and presentation.
  f.height = gdt->AddDimension();
  OccHandle<XCAFDimTolObjects_DimensionObject> dim = new XCAFDimTolObjects_DimensionObject();
  dim->SetType(XCAFDimTolObjects_DimensionType_Location_LinearDistance);
  dim->SetValue(20.0);
  dim->SetUpperTolValue(0.1);
  dim->SetLowerTolValue(-0.1);
  dim->SetPlane(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0)));
  dim->SetPointTextAttach(gp_Pnt(110, 0, 10));
  dim->SetPoint(gp_Pnt(100, 0, 0));
  dim->SetPoint2(gp_Pnt(100, 0, 20));
  TopoDS_Compound lines;
  BRep_Builder builder;
  builder.MakeCompound(lines);
  builder.Add(lines, BRepBuilderAPI_MakeEdge(gp_Pnt(105, 0, 0), gp_Pnt(105, 0, 20)).Edge());
  dim->SetPresentation(lines, hs("Height"));
  XCAFDoc_Dimension::Set(f.height)->SetObject(dim);
  gdt->SetDimension(f.top, f.bottom, f.height);

  // A note OCCT keeps only graphically.
  f.label = gdt->AddDimension();
  OccHandle<XCAFDimTolObjects_DimensionObject> note = new XCAFDimTolObjects_DimensionObject();
  note->SetType(XCAFDimTolObjects_DimensionType_CommonLabel);
  XCAFDoc_Dimension::Set(f.label)->SetObject(note);
  gdt->SetDimension(f.side, f.label);

  // Saved view.
  auto views = XCAFDoc_DocumentTool::ViewTool(main);
  auto clipping = XCAFDoc_DocumentTool::ClippingPlaneTool(main);
  f.view = views->AddView();
  OccHandle<XCAFView_Object> camera = new XCAFView_Object();
  camera->SetName(hs("Front"));
  camera->SetType(XCAFView_ProjectionType_Parallel);
  camera->SetProjectionPoint(gp_Pnt(50, -200, 10));
  camera->SetViewDirection(gp_Dir(0, 1, 0));
  camera->SetUpDirection(gp_Dir(0, 0, 1));
  camera->SetZoomFactor(1.5);
  camera->SetWindowHorizontalSize(120.0);
  camera->SetWindowVerticalSize(80.0);
  camera->SetFrontPlaneDistance(5.0);
  XCAFDoc_View::Set(f.view)->SetObject(camera);
  const TDF_Label plane = clipping->AddClippingPlane(gp_Pln(gp_Pnt(50, 0, 0), gp_Dir(1, 0, 0)), "Cut", false);
  views->SetView(labels({f.part}), labels({f.flatness, f.height}), labels({plane}), f.view);
  return f;
}

} // namespace sc::test
