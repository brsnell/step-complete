#include "render/SceneStyle.h"

#include <AIS_DisplayMode.hxx>
#include <AIS_ListOfInteractive.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <gp_Ax2.hxx>

namespace sc::render {

namespace {

const Quantity_Color kPartColor(0.72, 0.75, 0.80, Quantity_TOC_sRGB);
const Quantity_Color kEdgeColor(0.15, 0.16, 0.18, Quantity_TOC_sRGB);

} // namespace

namespace {

// Sets drawer attributes for the style and returns the AIS display mode to use.
int configureDrawer(const OccHandle<AIS_Shape>& shape, DisplayStyle style) {
  const OccHandle<Prs3d_Drawer>& drawer = shape->Attributes();
  const bool edges = style == DisplayStyle::ShadedWithEdges;
  drawer->SetFaceBoundaryDraw(edges);
  if (edges) {
    drawer->SetFaceBoundaryAspect(new Prs3d_LineAspect(kEdgeColor, Aspect_TOL_SOLID, 1.0));
  }
  return style == DisplayStyle::Wireframe ? AIS_WireFrame : AIS_Shaded;
}

} // namespace

void configureShapeStyle(const OccHandle<AIS_Shape>& shape, DisplayStyle style) {
  shape->SetDisplayMode(configureDrawer(shape, style));
}

void applyDisplayStyle(const OccHandle<AIS_InteractiveContext>& context, DisplayStyle style) {
  AIS_ListOfInteractive objects;
  context->DisplayedObjects(objects);
  for (AIS_ListOfInteractive::Iterator it(objects); it.More(); it.Next()) {
    OccHandle<AIS_Shape> shape = OccHandle<AIS_Shape>::DownCast(it.Value());
    if (shape.IsNull()) {
      continue;
    }
    // Let the context switch modes itself: it ignores a mode the object already reports.
    const int mode = configureDrawer(shape, style);
    context->SetDisplayMode(shape, mode, false);
    context->Redisplay(shape, false); // recompute for changed edge drawing
  }
}

OccHandle<AIS_Shape> makeDemoPart() {
  const TopoDS_Shape plate = BRepPrimAPI_MakeBox(gp_Pnt(-50, -30, 0), 100.0, 60.0, 12.0).Shape();
  const TopoDS_Shape boss = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 12), gp::DZ()), 18.0, 20.0).Shape();
  const TopoDS_Shape body = BRepAlgoAPI_Fuse(plate, boss).Shape();

  TopoDS_Shape part =
      BRepAlgoAPI_Cut(body, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1), gp::DZ()), 8.0, 40.0).Shape())
          .Shape();
  for (const double x : {-38.0, 38.0}) {
    part =
        BRepAlgoAPI_Cut(part, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, 0, -1), gp::DZ()), 5.0, 20.0).Shape())
            .Shape();
  }

  OccHandle<AIS_Shape> shape = new AIS_Shape(part);
  shape->SetColor(kPartColor);
  configureShapeStyle(shape, DisplayStyle::ShadedWithEdges);
  return shape;
}

} // namespace sc::render
