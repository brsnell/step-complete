#include "render/PmiLabel.h"

#include "render/PmiFont.h"

#include <Font_FTFont.hxx>
#include <Font_FontAspect.hxx>
#include <Font_TextFormatter.hxx>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_AspectText3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <NCollection_String.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_Text.hxx>
#include <Prs3d_TextAspect.hxx>
#include <Quantity_Color.hxx>
#include <Select3D_SensitiveTriangle.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <SelectMgr_Selection.hxx>
#include <TCollection_ExtendedString.hxx>

#include <algorithm>
#include <cmath>

namespace sc::render {

namespace {

// Annotations: dark blue, readable on the light background and on part colors.
const Quantity_Color kInk(0.05, 0.20, 0.55, Quantity_TOC_sRGB);
const Quantity_Color kPaper(1.0, 1.0, 1.0, Quantity_TOC_sRGB);
const Quantity_Color kHoverInk(0.20, 0.55, 0.95, Quantity_TOC_sRGB);
const Quantity_Color kSelectedInk(1.0, 0.62, 0.10, Quantity_TOC_sRGB);

// Sizes in logical pixels (scaled by the device pixel ratio).
constexpr double kFontSize = 13.0;
constexpr double kRowHeight = 24.0;
constexpr double kRowGap = 4.0;
constexpr double kCellPadding = 7.0;
constexpr double kDatumStem = 12.0;

// Width of `text` in pixels when drawn at `fontSize` with the bundled font, as OCCT
// lays it out.
double textWidth(const std::string& text, double fontSize) {
  if (text.empty()) {
    return 0.0;
  }
  const auto size = static_cast<unsigned int>(std::lround(fontSize));
  try {
    const OccHandle<Font_FTFont> font =
        Font_FTFont::FindAndCreate(pmiFontName(), Font_FontAspect_Regular, Font_FTFontParams(size, 72));
    if (!font.IsNull()) {
      OccHandle<Font_TextFormatter> formatter = new Font_TextFormatter();
      formatter->SetupAlignment(Graphic3d_HTA_LEFT, Graphic3d_VTA_TOP);
      formatter->Append(NCollection_String(text.c_str()), *font);
      formatter->Format();
      return static_cast<double>(formatter->ResultWidth());
    }
  } catch (...) {
    // fall through to the estimate
  }
  return static_cast<double>(text.size()) * fontSize * 0.6;
}

} // namespace

double PmiLabel::rowHeight(double dpr) {
  return std::round(kRowHeight * dpr);
}

double PmiLabel::stackPitch(double dpr) {
  return rowHeight(dpr) + std::round(kRowGap * dpr);
}

PmiLabel::PmiLabel(const core::PmiItem& item, const gp_Pnt& worldAnchor, int stackIndex, double dpr)
    : m_id(item.id), m_anchor(worldAnchor), m_stackIndex(std::max(0, stackIndex)),
      m_dpr(dpr > 0.0 ? dpr : 1.0) {
  layout(item);
  SetTransformPersistence(new Graphic3d_TransformPers(Graphic3d_TMF_ZoomRotatePers, worldAnchor));
  // Over the model, never cut by the surface the anchor lies on.
  SetZLayer(Graphic3d_ZLayerId_Topmost);

  // Hover and selection draw a box around the label: the default is to repaint the whole
  // presentation in the highlight color, which would fill the box and hide its text.
  for (const bool selected : {false, true}) {
    OccHandle<Prs3d_Drawer> style = new Prs3d_Drawer();
    style->SetMethod(Aspect_TOHM_BOUNDBOX);
    style->SetColor(selected ? kSelectedInk : kHoverInk);
    style->SetZLayer(Graphic3d_ZLayerId_Topmost);
    if (selected) {
      SetHilightAttributes(style);
    } else {
      SetDynamicHilightAttributes(style);
    }
  }
}

void PmiLabel::layout(const core::PmiItem& item) {
  m_rowHeight = rowHeight(m_dpr);
  m_centerY = -m_stackIndex * stackPitch(m_dpr);
  const double fontSize = kFontSize * m_dpr;
  const double padding = std::round(kCellPadding * m_dpr);

  std::vector<std::string> texts;
  if (item.kind == core::PmiKind::GeometricTolerance && !item.frame.empty()) {
    texts = item.frame;
  } else {
    texts.push_back(!item.text.empty() ? item.text : item.name);
  }
  if (item.kind == core::PmiKind::Datum) {
    m_stem = std::round(kDatumStem * m_dpr);
  }

  m_boxLeft = m_stem;
  double x = m_boxLeft;
  for (std::string& text : texts) {
    Cell cell;
    cell.x0 = x;
    // A datum letter or a single symbol makes a square cell.
    cell.x1 = x + std::max(m_rowHeight, std::ceil(textWidth(text, fontSize)) + 2.0 * padding);
    cell.text = std::move(text);
    x = cell.x1;
    m_cells.push_back(std::move(cell));
  }
  m_boxWidth = x - m_boxLeft;
}

std::vector<std::pair<double, double>> PmiLabel::cellExtents() const {
  std::vector<std::pair<double, double>> extents;
  for (const Cell& cell : m_cells) {
    extents.emplace_back(cell.x0, cell.x1);
  }
  return extents;
}

void PmiLabel::Compute(const OccHandle<PrsMgr_PresentationManager>&, const OccHandle<Prs3d_Presentation>& prs,
                       const int mode) {
  if (mode != 0 || m_cells.empty()) {
    return;
  }
  const double top = m_centerY + m_rowHeight / 2.0;
  const double bottom = m_centerY - m_rowHeight / 2.0;
  const double left = m_boxLeft;
  const double right = m_boxLeft + m_boxWidth;

  // Fill, pushed back a little so the border and text win the depth test.
  OccHandle<Graphic3d_Group> fillGroup = prs->NewGroup();
  OccHandle<Graphic3d_AspectFillArea3d> fill = new Graphic3d_AspectFillArea3d();
  fill->SetInteriorStyle(Aspect_IS_SOLID);
  fill->SetInteriorColor(kPaper);
  fill->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
  fill->SetFaceCulling(Graphic3d_TypeOfBackfacingModel_DoubleSided);
  fillGroup->SetGroupPrimitivesAspect(fill);
  OccHandle<Graphic3d_ArrayOfTriangles> quad = new Graphic3d_ArrayOfTriangles(4, 6);
  quad->AddVertex(gp_Pnt(left, bottom, 0.0));
  quad->AddVertex(gp_Pnt(right, bottom, 0.0));
  quad->AddVertex(gp_Pnt(right, top, 0.0));
  quad->AddVertex(gp_Pnt(left, top, 0.0));
  quad->AddEdges(1, 2, 3);
  quad->AddEdges(1, 3, 4);
  fillGroup->AddPrimitiveArray(quad);

  // Border, separators between cells and the datum stem.
  OccHandle<Graphic3d_Group> lineGroup = prs->NewGroup();
  lineGroup->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(kInk, Aspect_TOL_SOLID, 1.0 * m_dpr));
  OccHandle<Graphic3d_ArrayOfSegments> lines =
      new Graphic3d_ArrayOfSegments(static_cast<int>(8 + 2 * m_cells.size() + 2));
  auto segment = [&lines](double x0, double y0, double x1, double y1) {
    lines->AddVertex(gp_Pnt(x0, y0, 0.0));
    lines->AddVertex(gp_Pnt(x1, y1, 0.0));
  };
  segment(left, bottom, right, bottom);
  segment(right, bottom, right, top);
  segment(right, top, left, top);
  segment(left, top, left, bottom);
  for (size_t i = 1; i < m_cells.size(); ++i) {
    segment(m_cells[i].x0, bottom, m_cells[i].x0, top);
  }
  if (m_stem > 0.0) {
    segment(0.0, m_centerY, left, m_centerY);
  }
  lineGroup->AddPrimitiveArray(lines);

  // Cell texts.
  OccHandle<Graphic3d_Group> textGroup = prs->NewGroup();
  OccHandle<Prs3d_TextAspect> aspect = new Prs3d_TextAspect();
  aspect->SetFont(pmiFontName());
  aspect->SetColor(kInk);
  aspect->SetHeight(kFontSize * m_dpr);
  aspect->SetHorizontalJustification(Graphic3d_HTA_CENTER);
  aspect->SetVerticalJustification(Graphic3d_VTA_CENTER);
  textGroup->SetGroupPrimitivesAspect(aspect->Aspect());
  for (const Cell& cell : m_cells) {
    Prs3d_Text::Draw(textGroup, aspect, TCollection_ExtendedString(cell.text.c_str(), true),
                     gp_Pnt((cell.x0 + cell.x1) / 2.0, m_centerY, 0.0));
  }
}

void PmiLabel::ComputeSelection(const OccHandle<SelectMgr_Selection>& selection, const int mode) {
  if (mode != 0 || m_cells.empty()) {
    return;
  }
  OccHandle<SelectMgr_EntityOwner> owner = new SelectMgr_EntityOwner(this, 7);
  const gp_Pnt a(m_boxLeft, m_centerY - m_rowHeight / 2.0, 0.0);
  const gp_Pnt b(m_boxLeft + m_boxWidth, a.Y(), 0.0);
  const gp_Pnt c(b.X(), m_centerY + m_rowHeight / 2.0, 0.0);
  const gp_Pnt d(a.X(), c.Y(), 0.0);
  selection->Add(new Select3D_SensitiveTriangle(owner, a, b, c));
  selection->Add(new Select3D_SensitiveTriangle(owner, a, c, d));
}

} // namespace sc::render
