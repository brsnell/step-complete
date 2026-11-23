#include "render/ColorMapPresentation.h"

#include <AIS_ColorScale.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_MaterialAspect.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <NCollection_Sequence.hxx>
#include <NCollection_Vec2.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_Presentation.hxx>
#include <Select3D_SensitiveTriangulation.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <SelectMgr_Selection.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TopLoc_Location.hxx>

#include <algorithm>
#include <cmath>

namespace sc::render {

namespace {

constexpr int NoHighlightMode = 99; // a display mode with nothing in it

} // namespace

ColorMapPresentation::ColorMapPresentation(OccHandle<Poly_Triangulation> mesh, const gp_Trsf& placement)
    : _mesh(std::move(mesh)) {
  SetLocalTransformation(placement);
  // Hovering would paint the whole part in the highlight color and hide the colors: hover
  // highlighting uses a display mode that draws nothing.
  OccHandle<Prs3d_Drawer> hover = new Prs3d_Drawer();
  hover->SetDisplayMode(NoHighlightMode);
  SetDynamicHilightAttributes(hover);
  // Selecting would paint the part in one color too: a selected part shows its bounding box.
  OccHandle<Prs3d_Drawer> selected = new Prs3d_Drawer();
  selected->SetMethod(Aspect_TOHM_BOUNDBOX);
  selected->SetColor(Quantity_NOC_CYAN1);
  SetHilightAttributes(selected);
}

void ColorMapPresentation::Compute(const OccHandle<PrsMgr_PresentationManager>&,
                                   const OccHandle<Prs3d_Presentation>& prs, const int mode) {
  if (mode == NoHighlightMode || _mesh.IsNull()) {
    return;
  }
  OccHandle<Graphic3d_Group> group = prs->NewGroup();
  OccHandle<Graphic3d_AspectFillArea3d> fill = new Graphic3d_AspectFillArea3d();
  fill->SetInteriorStyle(Aspect_IS_SOLID);
  fill->SetInteriorColor(Quantity_NOC_WHITE);
  Graphic3d_MaterialAspect material(Graphic3d_NameOfMaterial_Plastered);
  material.SetColor(Quantity_NOC_WHITE);
  fill->SetFrontMaterial(material);
  fill->SetBackMaterial(material);
  group->SetGroupPrimitivesAspect(fill);

  OccHandle<Graphic3d_ArrayOfTriangles> triangles =
      new Graphic3d_ArrayOfTriangles(_mesh->NbNodes(), 3 * _mesh->NbTriangles(),
                                     Graphic3d_ArrayFlags_VertexNormal | Graphic3d_ArrayFlags_VertexColor);
  for (int i = 1; i <= _mesh->NbNodes(); ++i) {
    triangles->AddVertex(_mesh->Node(i), _mesh->Normal(i), nodeColor(i));
  }
  for (int i = 1; i <= _mesh->NbTriangles(); ++i) {
    int n1 = 0;
    int n2 = 0;
    int n3 = 0;
    _mesh->Triangle(i).Get(n1, n2, n3);
    triangles->AddEdges(n1, n2, n3);
  }
  group->AddPrimitiveArray(triangles);
}

void ColorMapPresentation::ComputeSelection(const OccHandle<SelectMgr_Selection>& selection, const int mode) {
  if (mode != 0 || _mesh.IsNull()) {
    return;
  }
  OccHandle<SelectMgr_EntityOwner> owner = new SelectMgr_EntityOwner(this, 5);
  selection->Add(new Select3D_SensitiveTriangulation(owner, _mesh, TopLoc_Location(), true));
}

OccHandle<AIS_ColorScale> makeColorLegend(double min, double max,
                                          const std::function<Quantity_Color(double)>& colorAt,
                                          const std::string& title, int decimals, int intervals) {
  OccHandle<AIS_ColorScale> legend = new AIS_ColorScale();
  legend->SetRange(min, max);
  legend->SetNumberOfIntervals(intervals);
  NCollection_Sequence<Quantity_Color> colors;
  for (int i = 0; i < intervals; ++i) {
    colors.Append(colorAt(min + (i + 0.5) / intervals * (max - min)));
  }
  legend->SetColorType(Aspect_TOCSD_USER);
  legend->SetColors(colors);
  const std::string format = "%." + std::to_string(std::clamp(decimals, 0, 6)) + "f";
  legend->SetFormat(TCollection_AsciiString(format.c_str()));
  legend->SetTitle(TCollection_ExtendedString(title.c_str(), true));
  legend->SetSize(80, 260);
  legend->SetTextHeight(13);
  legend->SetTransformPersistence(
      new Graphic3d_TransformPers(Graphic3d_TMF_2d, Aspect_TOTP_LEFT_LOWER, NCollection_Vec2<int>(16, 16)));
  legend->SetZLayer(Graphic3d_ZLayerId_TopOSD);
  return legend;
}

Quantity_Color hueColor(double degrees) {
  const double hue = std::clamp(degrees, 0.0, 240.0) / 60.0; // sextant
  const double x = 1.0 - std::abs(std::fmod(hue, 2.0) - 1.0);
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  if (hue < 1.0) {
    r = 1.0;
    g = x;
  } else if (hue < 2.0) {
    r = x;
    g = 1.0;
  } else if (hue < 3.0) {
    g = 1.0;
    b = x;
  } else {
    g = x;
    b = 1.0;
  }
  return Quantity_Color(r, g, b, Quantity_TOC_sRGB);
}

} // namespace sc::render
