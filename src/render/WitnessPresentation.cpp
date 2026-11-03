#include "render/WitnessPresentation.h"

#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Graphic3d_AspectText3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_Text.hxx>
#include <Prs3d_TextAspect.hxx>
#include <TCollection_ExtendedString.hxx>

namespace sc::render {

namespace {

// Orange reads well against both the light background and typical part colors,
// and is distinct from the blue selection highlight.
const Quantity_Color kWitnessColor(0.95, 0.45, 0.05, Quantity_TOC_sRGB);
const Quantity_Color kLabelBackground(1.0, 1.0, 1.0, Quantity_TOC_sRGB);
const Quantity_Color kLabelText(0.10, 0.10, 0.12, Quantity_TOC_sRGB);

} // namespace

WitnessPresentation::WitnessPresentation(core::Witness witness, double devicePixelRatio)
    : m_witness(std::move(witness)), m_dpr(devicePixelRatio > 0.0 ? devicePixelRatio : 1.0) {
  // Its own depth range on top of the scene: never hidden behind the part it measures.
  SetZLayer(Graphic3d_ZLayerId_Topmost);
  SetInfiniteState(false);
}

void WitnessPresentation::Compute(const OccHandle<PrsMgr_PresentationManager>&,
                                  const OccHandle<Prs3d_Presentation>& prs, const int mode) {
  if (mode != 0) {
    return;
  }

  int vertexCount = 0;
  int boundCount = 0;
  for (const auto& polyline : m_witness.polylines) {
    if (polyline.size() >= 2) {
      vertexCount += static_cast<int>(polyline.size());
      ++boundCount;
    }
  }
  if (boundCount > 0) {
    OccHandle<Graphic3d_Group> lines = prs->NewGroup();
    lines->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(kWitnessColor, Aspect_TOL_SOLID, 2.0 * m_dpr));
    OccHandle<Graphic3d_ArrayOfPolylines> array = new Graphic3d_ArrayOfPolylines(vertexCount, boundCount);
    for (const auto& polyline : m_witness.polylines) {
      if (polyline.size() < 2) {
        continue;
      }
      array->AddBound(static_cast<int>(polyline.size()));
      for (const gp_Pnt& p : polyline) {
        array->AddVertex(p);
      }
    }
    lines->AddPrimitiveArray(array);
  }

  if (!m_witness.markers.empty()) {
    OccHandle<Graphic3d_Group> markers = prs->NewGroup();
    markers->SetGroupPrimitivesAspect(
        new Graphic3d_AspectMarker3d(Aspect_TOM_BALL, kWitnessColor, 1.5 * m_dpr));
    OccHandle<Graphic3d_ArrayOfPoints> points =
        new Graphic3d_ArrayOfPoints(static_cast<int>(m_witness.markers.size()));
    for (const gp_Pnt& p : m_witness.markers) {
      points->AddVertex(p);
    }
    markers->AddPrimitiveArray(points);
  }

  if (!m_witness.label.empty()) {
    OccHandle<Graphic3d_Group> label = prs->NewGroup();
    OccHandle<Prs3d_TextAspect> aspect = new Prs3d_TextAspect();
    aspect->SetColor(kLabelText);
    aspect->SetHeight(14.0 * m_dpr);
    aspect->SetHorizontalJustification(Graphic3d_HTA_CENTER);
    aspect->SetVerticalJustification(Graphic3d_VTA_CENTER);
    // A filled box behind the text keeps the value readable over any geometry.
    aspect->Aspect()->SetDisplayType(Aspect_TODT_SUBTITLE);
    aspect->Aspect()->SetColorSubTitle(kLabelBackground);
    label->SetGroupPrimitivesAspect(aspect->Aspect());
    Prs3d_Text::Draw(label, aspect, TCollection_ExtendedString(m_witness.label.c_str(), true),
                     m_witness.labelAnchor);
  }
}

} // namespace sc::render
