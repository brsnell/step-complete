#pragma once

#include "core/OccHandle.h"

#include <AIS_InteractiveObject.hxx>
#include <Poly_Triangulation.hxx>
#include <Quantity_Color.hxx>
#include <gp_Trsf.hxx>

#include <functional>
#include <string>

class AIS_ColorScale;

namespace sc::render {

// A part's mesh drawn with a color per node, placed where the part is: the wall thickness and
// model comparison views. It stands in for a part through SceneManager::setPartOverride, so it
// selects as the whole part (mode 0). Hovering does not highlight it and a selected one shows its
// bounding box: painting it in one color would hide the colors. Every display mode draws the
// colors (display styles do not apply).
class ColorMapPresentation : public AIS_InteractiveObject {
  DEFINE_STANDARD_RTTI_INLINE(ColorMapPresentation, AIS_InteractiveObject)

public:
  ColorMapPresentation(OccHandle<Poly_Triangulation> mesh, const gp_Trsf& placement);

  // In the part's own coordinates, with normals.
  const OccHandle<Poly_Triangulation>& mesh() const { return _mesh; }

protected:
  // The color of node i (1-based); called when the presentation is computed.
  virtual Quantity_Color nodeColor(int node) const = 0;

private:
  void Compute(const OccHandle<PrsMgr_PresentationManager>& manager, const OccHandle<Prs3d_Presentation>& prs,
               int mode) override;
  void ComputeSelection(const OccHandle<SelectMgr_Selection>& selection, int mode) override;

  OccHandle<Poly_Triangulation> _mesh;
};

// The same color for every node: parts the comparison shows by their kind of change.
class UniformColorPresentation : public ColorMapPresentation {
  DEFINE_STANDARD_RTTI_INLINE(UniformColorPresentation, ColorMapPresentation)

public:
  UniformColorPresentation(OccHandle<Poly_Triangulation> mesh, const gp_Trsf& placement,
                           const Quantity_Color& color)
      : ColorMapPresentation(std::move(mesh), placement), _color(color) {}

protected:
  Quantity_Color nodeColor(int) const override { return _color; }

private:
  Quantity_Color _color;
};

// A legend in the lower-left corner of the view: 'intervals' bands from min to max, colored by
// colorAt (in the shown values), with the title above and labels in the given decimals. Not
// selectable; the caller displays it.
OccHandle<AIS_ColorScale> makeColorLegend(double min, double max,
                                          const std::function<Quantity_Color(double)>& colorAt,
                                          const std::string& title, int decimals, int intervals = 10);

// A fully saturated hue from 0 (red) to 240 (blue) degrees, in sRGB.
Quantity_Color hueColor(double degrees);

} // namespace sc::render
