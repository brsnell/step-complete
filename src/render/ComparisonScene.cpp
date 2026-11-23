#include "render/ComparisonScene.h"

#include "core/PartMesh.h"
#include "render/ColorMapPresentation.h"
#include "render/SceneManager.h"

#include <AIS_ColorScale.hxx>
#include <AIS_Shape.hxx>
#include <BRepBndLib.hxx>
#include <Prs3d_Drawer.hxx>

#include <algorithm>
#include <cmath>

namespace sc::render {

namespace {

// A changed part, colored by its signed deviation per node.
class DeviationPresentation : public ColorMapPresentation {
  DEFINE_STANDARD_RTTI_INLINE(DeviationPresentation, ColorMapPresentation)

public:
  DeviationPresentation(const core::PartChange& change, const gp_Trsf& placement, double tolerance,
                        double range)
      : ColorMapPresentation(change.mesh, placement), _deviation(change.deviation), _tolerance(tolerance),
        _range(range) {}

protected:
  Quantity_Color nodeColor(int node) const override {
    const size_t i = static_cast<size_t>(node - 1);
    return ComparisonScene::deviationColor(i < _deviation.size() ? _deviation[i] : 0.0, _tolerance, _range);
  }

private:
  std::vector<float> _deviation;
  double _tolerance;
  double _range;
};

} // namespace

ComparisonScene::ComparisonScene(const OccHandle<AIS_InteractiveContext>& context, SceneManager& scene)
    : _context(context), _scene(scene) {}

ComparisonScene::~ComparisonScene() {
  clear();
}

Quantity_Color ComparisonScene::colorOf(core::ChangeKind kind) {
  switch (kind) {
  case core::ChangeKind::Unchanged:
    return Quantity_Color(0.78, 0.78, 0.78, Quantity_TOC_sRGB);
  case core::ChangeKind::Moved:
    return Quantity_Color(0.62, 0.35, 0.85, Quantity_TOC_sRGB);
  case core::ChangeKind::Changed:
    return Quantity_Color(0.2, 0.8, 0.2, Quantity_TOC_sRGB);
  case core::ChangeKind::Added:
    return Quantity_Color(1.0, 0.55, 0.0, Quantity_TOC_sRGB);
  case core::ChangeKind::Removed:
    return Quantity_Color(0.9, 0.1, 0.1, Quantity_TOC_sRGB);
  }
  return Quantity_Color(Quantity_NOC_WHITE);
}

Quantity_Color ComparisonScene::deviationColor(double value, double tolerance, double range) {
  if (std::abs(value) <= tolerance) {
    return hueColor(120.0); // green: within the tolerance
  }
  const double span = std::max(range - tolerance, 1e-12);
  const double s = std::clamp((std::abs(value) - tolerance) / span, 0.0, 1.0);
  // Towards red where material was added, towards blue where it was taken away.
  return hueColor(value > 0.0 ? 90.0 * (1.0 - s) : 150.0 + 90.0 * s);
}

void ComparisonScene::show(std::shared_ptr<const core::ComparisonResult> result,
                           const core::AssemblyIndex& current,
                           std::shared_ptr<const core::AssemblyIndex> other, double tolerance) {
  clear();
  if (!result || !other) {
    return;
  }
  _result = std::move(result);
  _other = std::move(other);
  _tolerance = tolerance;
  _range = std::max(_result->maxDeviation, 2.0 * tolerance);
  std::map<std::string, OccHandle<Poly_Triangulation>> meshes; // by definition
  for (size_t i = 0; i < _result->changes.size(); ++i) {
    const core::PartChange& change = _result->changes[i];
    if (change.current != core::NoNode) {
      const gp_Trsf placement = current.node(change.current).location.Transformation();
      OccHandle<AIS_InteractiveObject> presentation;
      if (change.kind == core::ChangeKind::Changed && !change.mesh.IsNull() && !change.deviation.empty()) {
        presentation = new DeviationPresentation(change, placement, _tolerance, _range);
      } else {
        const auto [it, inserted] = meshes.try_emplace(current.prototypeId(change.current));
        if (inserted) {
          it->second = core::partMesh(current.prototypeShape(change.current));
        }
        if (it->second.IsNull()) {
          continue; // nothing to draw: the part keeps its own look
        }
        presentation = new UniformColorPresentation(it->second, placement, colorOf(change.kind));
      }
      _scene.setPartOverride(change.current, presentation);
      _parts.push_back(change.current);
    } else if (change.other != core::NoNode) {
      OccHandle<AIS_Shape> ghost = new AIS_Shape(_other->worldShape(change.other));
      ghost->SetColor(colorOf(core::ChangeKind::Removed));
      ghost->SetTransparency(0.6);
      ghost->Attributes()->SetFaceBoundaryDraw(false);
      _context->Display(ghost, AIS_Shaded, -1, false); // not selectable
      _ghosts.emplace(static_cast<int>(i), ghost);
    }
  }
  updateLegend();
}

void ComparisonScene::clear() {
  for (const core::NodeId part : _parts) {
    _scene.setPartOverride(part, nullptr);
  }
  _parts.clear();
  for (const auto& [index, ghost] : _ghosts) {
    _context->Remove(ghost, false);
  }
  _ghosts.clear();
  if (!_legend.IsNull()) {
    _context->Remove(_legend, false);
    _legend.Nullify();
  }
  _result.reset();
  _other.reset();
}

void ComparisonScene::setUnit(core::LengthUnit unit, int decimals) {
  _unit = unit;
  _decimals = decimals;
  updateLegend();
}

bool ComparisonScene::hasLegend() const {
  return !_legend.IsNull() && _context->IsDisplayed(_legend);
}

Bnd_Box ComparisonScene::ghostBox(int changeIndex) const {
  Bnd_Box box;
  const auto it = _ghosts.find(changeIndex);
  if (it != _ghosts.end()) {
    BRepBndLib::Add(OccHandle<AIS_Shape>::DownCast(it->second)->Shape(), box, true);
  }
  return box;
}

void ComparisonScene::updateLegend() {
  if (!_legend.IsNull()) {
    _context->Remove(_legend, false);
    _legend.Nullify();
  }
  if (!_result || _result->changed == 0) {
    return; // nothing is colored by deviation
  }
  const double perUnit = core::millimetersPer(_unit);
  const double tolerance = _tolerance;
  const double range = _range;
  _legend = makeColorLegend(
      -_range / perUnit, _range / perUnit,
      [tolerance, range, perUnit](double value) { return deviationColor(value * perUnit, tolerance, range); },
      "Deviation (" + std::string(core::symbol(_unit)) + ")", _decimals, 11);
  _context->Display(_legend, 0, -1, false);
}

} // namespace sc::render
