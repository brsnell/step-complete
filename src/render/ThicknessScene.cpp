#include "render/ThicknessScene.h"

#include "render/ColorMapPresentation.h"
#include "render/SceneManager.h"

#include <AIS_ColorScale.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <string>

namespace sc::render {

namespace {

// One part instance's thickness map, placed where the instance is.
class ThicknessPresentation : public ColorMapPresentation {
  DEFINE_STANDARD_RTTI_INLINE(ThicknessPresentation, ColorMapPresentation)

public:
  ThicknessPresentation(std::shared_ptr<const core::ThicknessMap> map, const gp_Trsf& placement)
      : ColorMapPresentation(map->mesh, placement), _map(std::move(map)) {}

  void setLimits(double thin, double thick) {
    _thin = thin;
    _thick = thick;
  }

  const core::ThicknessMap& map() const { return *_map; }

protected:
  Quantity_Color nodeColor(int node) const override {
    return ThicknessScene::colorOf(_map->thickness[static_cast<size_t>(node - 1)], _thin, _thick);
  }

private:
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
  return hueColor(240.0 * s); // red (thin) to blue (thick)
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
    _parts.push_back(part);
  }
  updateLegend();
}

void ThicknessScene::clear() {
  for (const core::NodeId part : _parts) { // only ours: another view may override other parts
    _scene.setPartOverride(part, nullptr);
  }
  _parts.clear();
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
  const double thin = _thin;
  const double thick = _thick;
  OccHandle<AIS_ColorScale> legend = makeColorLegend(
      _thin / perUnit, _thick / perUnit,
      [thin, thick, perUnit](double value) { return colorOf(value * perUnit, thin, thick); },
      "Wall thickness (" + std::string(core::symbol(_unit)) + ")", _decimals);
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
