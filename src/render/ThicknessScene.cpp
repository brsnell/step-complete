#include "render/ThicknessScene.h"

#include "render/SceneManager.h"

#include <AIS_ColorScale.hxx>
#include <Aspect_SequenceOfColor.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_Presentation.hxx>
#include <Select3D_SensitiveTriangulation.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <SelectMgr_Selection.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_ExtendedString.hxx>

#include <algorithm>
#include <cmath>
#include <string>

namespace sc::render {

namespace {

constexpr int LegendIntervals = 10;
constexpr int NoHighlightMode = 99; // a display mode with nothing in it

// One part instance's thickness map, placed where the instance is.
class ThicknessPresentation : public AIS_InteractiveObject {
  DEFINE_STANDARD_RTTI_INLINE(ThicknessPresentation, AIS_InteractiveObject)

public:
  ThicknessPresentation(std::shared_ptr<const core::ThicknessMap> map, const gp_Trsf& placement)
      : _map(std::move(map)) {
    SetLocalTransformation(placement);
    // Hovering would paint the whole part in the highlight color and hide the very colors the
    // readout is about: hover highlighting uses a display mode that draws nothing.
    OccHandle<Prs3d_Drawer> hover = new Prs3d_Drawer();
    hover->SetDisplayMode(NoHighlightMode);
    SetDynamicHilightAttributes(hover);
  }

  void setLimits(double thin, double thick) {
    _thin = thin;
    _thick = thick;
  }

  const core::ThicknessMap& map() const { return *_map; }

private:
  // Every display mode draws the colored mesh: display styles do not apply to this view.
  void Compute(const OccHandle<PrsMgr_PresentationManager>&, const OccHandle<Prs3d_Presentation>& prs,
               const int mode) override {
    if (mode == NoHighlightMode) {
      return;
    }
    const OccHandle<Poly_Triangulation>& mesh = _map->mesh;
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
        new Graphic3d_ArrayOfTriangles(mesh->NbNodes(), 3 * mesh->NbTriangles(),
                                       Graphic3d_ArrayFlags_VertexNormal | Graphic3d_ArrayFlags_VertexColor);
    for (int i = 1; i <= mesh->NbNodes(); ++i) {
      triangles->AddVertex(
          mesh->Node(i), mesh->Normal(i),
          ThicknessScene::colorOf(_map->thickness[static_cast<size_t>(i - 1)], _thin, _thick));
    }
    for (int i = 1; i <= mesh->NbTriangles(); ++i) {
      int n1 = 0;
      int n2 = 0;
      int n3 = 0;
      mesh->Triangle(i).Get(n1, n2, n3);
      triangles->AddEdges(n1, n2, n3);
    }
    group->AddPrimitiveArray(triangles);
  }

  // Mode 0 only: the whole part, like a part's own presentation without sub-shape picking.
  void ComputeSelection(const OccHandle<SelectMgr_Selection>& selection, const int mode) override {
    if (mode != 0) {
      return;
    }
    OccHandle<SelectMgr_EntityOwner> owner = new SelectMgr_EntityOwner(this, 5);
    selection->Add(new Select3D_SensitiveTriangulation(owner, _map->mesh, TopLoc_Location(), true));
  }

  std::shared_ptr<const core::ThicknessMap> _map;
  double _thin = 0.0;
  double _thick = 1.0;
};

} // namespace

ThicknessScene::ThicknessScene(const OccHandle<AIS_InteractiveContext>& context, SceneManager& scene)
    : _context(context), _scene(scene) {}

ThicknessScene::~ThicknessScene() {
  clear();
}

Quantity_Color ThicknessScene::colorOf(double thickness, double thin, double thick) {
  if (std::isnan(thickness)) {
    return Quantity_Color(0.6, 0.6, 0.6, Quantity_TOC_RGB);
  }
  const double span = thick - thin;
  const double s = span > 0.0 ? std::clamp((thickness - thin) / span, 0.0, 1.0) : 0.5;
  // Hue 0 (red, thin) to 240 (blue, thick) at full saturation and brightness, in sRGB.
  const double hue = 240.0 * s / 60.0; // sextant
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

void ThicknessScene::show(std::shared_ptr<const core::ThicknessResult> result,
                          const core::AssemblyIndex& index) {
  clear();
  if (!result) {
    return;
  }
  _result = std::move(result);
  _thin = _result->min;
  _thick = std::max(_result->max, _result->min);
  for (const core::NodeId part : _result->parts) {
    const std::shared_ptr<const core::ThicknessMap> map = _result->mapOf(index, part);
    if (!map) {
      continue; // nothing to color: the part keeps its own look
    }
    OccHandle<ThicknessPresentation> presentation =
        new ThicknessPresentation(map, index.node(part).location.Transformation());
    presentation->setLimits(_thin, _thick);
    _scene.setPartOverride(part, presentation);
    _presentations.push_back(presentation);
  }
  updateLegend();
}

void ThicknessScene::clear() {
  _scene.clearPartOverrides();
  _presentations.clear();
  if (!_legend.IsNull()) {
    _context->Remove(_legend, false);
    _legend.Nullify();
  }
  _result.reset();
}

void ThicknessScene::setLimits(double thin, double thick) {
  _thin = std::min(thin, thick);
  _thick = std::max(thin, thick);
  for (const OccHandle<AIS_InteractiveObject>& object : _presentations) {
    static_cast<ThicknessPresentation*>(object.get())->setLimits(_thin, _thick);
    if (_context->IsDisplayed(object)) {
      _context->Redisplay(object, false);
    } else {
      object->SetToUpdate();
    }
  }
  updateLegend();
}

void ThicknessScene::setUnit(core::LengthUnit unit, int decimals) {
  _unit = unit;
  _decimals = decimals;
  updateLegend();
}

bool ThicknessScene::hasLegend() const {
  return !_legend.IsNull() && _context->IsDisplayed(_legend);
}

void ThicknessScene::updateLegend() {
  if (!_legend.IsNull()) {
    _context->Remove(_legend, false);
    _legend.Nullify();
  }
  if (!_result || _presentations.empty()) {
    return;
  }
  const double perUnit = core::millimetersPer(_unit);
  OccHandle<AIS_ColorScale> legend = new AIS_ColorScale();
  legend->SetRange(_thin / perUnit, _thick / perUnit);
  legend->SetNumberOfIntervals(LegendIntervals);
  Aspect_SequenceOfColor colors;
  for (int i = 0; i < LegendIntervals; ++i) {
    const double s = (i + 0.5) / LegendIntervals;
    colors.Append(colorOf(_thin + s * (_thick - _thin), _thin, _thick));
  }
  legend->SetColorType(Aspect_TOCSD_USER);
  legend->SetColors(colors);
  legend->SetFormat(
      TCollection_AsciiString(("%." + std::to_string(std::clamp(_decimals, 0, 6)) + "f").c_str()));
  const std::string title = "Wall thickness (" + std::string(core::symbol(_unit)) + ")";
  legend->SetTitle(TCollection_ExtendedString(title.c_str(), true));
  legend->SetSize(80, 260);
  legend->SetTextHeight(13);
  legend->SetTransformPersistence(
      new Graphic3d_TransformPers(Graphic3d_TMF_2d, Aspect_TOTP_LEFT_LOWER, Graphic3d_Vec2i(16, 16)));
  legend->SetZLayer(Graphic3d_ZLayerId_TopOSD);
  _context->Display(legend, 0, -1, false); // not selectable
  _legend = legend;
}

std::optional<double> ThicknessScene::thicknessAt(const OccHandle<AIS_InteractiveObject>& object,
                                                  const gp_Pnt& world, const gp_Dir& viewDirection) const {
  const auto* presentation = dynamic_cast<const ThicknessPresentation*>(object.get());
  if (presentation == nullptr) {
    return std::nullopt;
  }
  const gp_Trsf toPart = presentation->LocalTransformation().Inverted();
  return core::thicknessAt(presentation->map(), world.Transformed(toPart), viewDirection.Transformed(toPart));
}

} // namespace sc::render
