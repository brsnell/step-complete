#include "render/PmiLabel.h"

#include "render/PmiFont.h"

#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_AspectText3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_ZLayerId.hxx>
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
const Quantity_Color Ink(0.05, 0.20, 0.55, Quantity_TOC_sRGB);
const Quantity_Color Paper(1.0, 1.0, 1.0, Quantity_TOC_sRGB);
const Quantity_Color HoverInk(0.20, 0.55, 0.95, Quantity_TOC_sRGB);
const Quantity_Color SelectedInk(1.0, 0.62, 0.10, Quantity_TOC_sRGB);

// Sizes in logical pixels (scaled by the device pixel ratio).
constexpr double FontSize = 13.0;
constexpr double RowHeight = 24.0;
constexpr double RowGap = 4.0;
constexpr double CellPadding = 7.0;
constexpr double DatumStem = 12.0;

} // namespace

double PmiLabel::rowHeight(double dpr) {
  return std::round(RowHeight * dpr);
}

double PmiLabel::stackPitch(double dpr) {
  return rowHeight(dpr) + std::round(RowGap * dpr);
}

void PmiLabel::setAnchorOffset(const gp_Vec& offset) {
  const gp_Pnt anchor = _baseAnchor.Translated(offset);
  if (anchor.IsEqual(_anchor, 0.0)) {
    return;
  }
  _anchor = anchor;
  SetTransformPersistence(new Graphic3d_TransformPers(Graphic3d_TMF_ZoomRotatePers, _anchor));
}

PmiLabel::PmiLabel(const core::PmiItem& item, const gp_Pnt& worldAnchor, int stackIndex, double dpr)
    : _id(item.id), _baseAnchor(worldAnchor), _anchor(worldAnchor), _stackIndex(std::max(0, stackIndex)),
      _dpr(dpr > 0.0 ? dpr : 1.0) {
  layout(item);
  SetTransformPersistence(new Graphic3d_TransformPers(Graphic3d_TMF_ZoomRotatePers, worldAnchor));
  // Over the model, never cut by the surface the anchor lies on.
  SetZLayer(Graphic3d_ZLayerId_Topmost);

  // Hover and selection draw a box around the label: the default is to repaint the whole
  // presentation in the highlight color, which would fill the box and hide its text.
  for (const bool selected : {false, true}) {
    OccHandle<Prs3d_Drawer> style = new Prs3d_Drawer();
    style->SetMethod(Aspect_TOHM_BOUNDBOX);
    style->SetColor(selected ? SelectedInk : HoverInk);
    style->SetZLayer(Graphic3d_ZLayerId_Topmost);
    if (selected) {
      SetHilightAttributes(style);
    } else {
      SetDynamicHilightAttributes(style);
    }
  }
}

void PmiLabel::layout(const core::PmiItem& item) {
  _rowHeight = rowHeight(_dpr);
  _centerY = -_stackIndex * stackPitch(_dpr);
  const double fontSize = FontSize * _dpr;
  const double padding = std::round(CellPadding * _dpr);

  std::vector<std::string> texts;
  if (item.kind == core::PmiKind::GeometricTolerance && !item.frame.empty()) {
    texts = item.frame;
  } else {
    texts.push_back(!item.text.empty() ? item.text : item.name);
  }
  if (item.kind == core::PmiKind::Datum) {
    _stem = std::round(DatumStem * _dpr);
  }

  _boxLeft = _stem;
  double x = _boxLeft;
  for (std::string& text : texts) {
    Cell cell;
    cell.x0 = x;
    // A datum letter or a single symbol makes a square cell.
    cell.x1 = x + std::max(_rowHeight, std::ceil(pmiTextWidth(text, fontSize)) + 2.0 * padding);
    cell.text = std::move(text);
    x = cell.x1;
    _cells.push_back(std::move(cell));
  }
  _boxWidth = x - _boxLeft;
}

void PmiLabel::setOffset(double dx, double dy) {
  const double shiftX = dx - _offsetX;
  for (Cell& cell : _cells) {
    cell.x0 += shiftX;
    cell.x1 += shiftX;
  }
  _boxLeft += shiftX;
  _centerY += dy - _offsetY;
  _offsetX = dx;
  _offsetY = dy;
}

std::vector<std::pair<double, double>> PmiLabel::cellExtents() const {
  std::vector<std::pair<double, double>> extents;
  for (const Cell& cell : _cells) {
    extents.emplace_back(cell.x0, cell.x1);
  }
  return extents;
}

void PmiLabel::Compute(const OccHandle<PrsMgr_PresentationManager>&, const OccHandle<Prs3d_Presentation>& prs,
                       const int mode) {
  if (mode != 0 || _cells.empty()) {
    return;
  }
  const double top = _centerY + _rowHeight / 2.0;
  const double bottom = _centerY - _rowHeight / 2.0;
  const double left = _boxLeft;
  const double right = _boxLeft + _boxWidth;

  // Fill, pushed back a little so the border and text win the depth test.
  OccHandle<Graphic3d_Group> fillGroup = prs->NewGroup();
  OccHandle<Graphic3d_AspectFillArea3d> fill = new Graphic3d_AspectFillArea3d();
  fill->SetInteriorStyle(Aspect_IS_SOLID);
  fill->SetInteriorColor(Paper);
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
  lineGroup->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(Ink, Aspect_TOL_SOLID, 1.0 * _dpr));
  OccHandle<Graphic3d_ArrayOfSegments> lines =
      new Graphic3d_ArrayOfSegments(static_cast<int>(8 + 2 * _cells.size() + 4));
  auto segment = [&lines](double x0, double y0, double x1, double y1) {
    lines->AddVertex(gp_Pnt(x0, y0, 0.0));
    lines->AddVertex(gp_Pnt(x1, y1, 0.0));
  };
  segment(left, bottom, right, bottom);
  segment(right, bottom, right, top);
  segment(right, top, left, top);
  segment(left, top, left, bottom);
  for (size_t i = 1; i < _cells.size(); ++i) {
    segment(_cells[i].x0, bottom, _cells[i].x0, top);
  }
  if (_stem > 0.0 && _offsetX == 0.0 && _offsetY == 0.0) {
    segment(0.0, _centerY, left, _centerY);
  }
  // A label displaced from its anchor (stacked or moved by declutter) gets a connector from
  // the anchor to the nearest point of its box.
  const double nearestX = std::clamp(0.0, left, right);
  const double nearestY = std::clamp(0.0, bottom, top);
  if (nearestX != 0.0 || nearestY != 0.0) {
    segment(0.0, 0.0, nearestX, nearestY);
  }
  lineGroup->AddPrimitiveArray(lines);

  // Cell texts.
  OccHandle<Graphic3d_Group> textGroup = prs->NewGroup();
  OccHandle<Prs3d_TextAspect> aspect = new Prs3d_TextAspect();
  aspect->SetFont(pmiFontName());
  aspect->SetColor(Ink);
  aspect->SetHeight(FontSize * _dpr);
  aspect->SetHorizontalJustification(Graphic3d_HTA_CENTER);
  aspect->SetVerticalJustification(Graphic3d_VTA_CENTER);
  textGroup->SetGroupPrimitivesAspect(aspect->Aspect());
  for (const Cell& cell : _cells) {
    Prs3d_Text::Draw(textGroup, aspect, TCollection_ExtendedString(cell.text.c_str(), true),
                     gp_Pnt((cell.x0 + cell.x1) / 2.0, _centerY, 0.0));
  }
}

void PmiLabel::ComputeSelection(const OccHandle<SelectMgr_Selection>& selection, const int mode) {
  if (mode != 0 || _cells.empty()) {
    return;
  }
  OccHandle<SelectMgr_EntityOwner> owner = new SelectMgr_EntityOwner(this, 7);
  const gp_Pnt bottomLeft(_boxLeft, _centerY - _rowHeight / 2.0, 0.0);
  const gp_Pnt bottomRight(_boxLeft + _boxWidth, bottomLeft.Y(), 0.0);
  const gp_Pnt topRight(bottomRight.X(), _centerY + _rowHeight / 2.0, 0.0);
  const gp_Pnt topLeft(bottomLeft.X(), topRight.Y(), 0.0);
  selection->Add(new Select3D_SensitiveTriangle(owner, bottomLeft, bottomRight, topRight));
  selection->Add(new Select3D_SensitiveTriangle(owner, bottomLeft, topRight, topLeft));
}

} // namespace sc::render
