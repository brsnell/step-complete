#pragma once

#include "core/AssemblyIndex.h"
#include "core/OccHandle.h"
#include "core/Units.h"
#include "core/WallThickness.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <Quantity_Color.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <memory>
#include <optional>
#include <vector>

class AIS_ColorScale;

namespace sc::render {

class SceneManager;

// The wall thickness view (docs/designs/wall-thickness.md): each analysed part instance shown as
// its thickness map, colored from red (at or below the thin limit) to blue (at or above the thick
// limit), with a legend in the lower-left corner. The presentations stand in for the parts
// through SceneManager overrides, so visibility, selection and section planes keep working.
// Never redraws; the caller invalidates the view.
class ThicknessScene {
public:
  ThicknessScene(const OccHandle<AIS_InteractiveContext>& context, SceneManager& scene);
  ~ThicknessScene();

  ThicknessScene(const ThicknessScene&) = delete;
  ThicknessScene& operator=(const ThicknessScene&) = delete;

  // Shows the result for its parts, with the limits set to its range.
  void show(std::shared_ptr<const core::ThicknessResult> result, const core::AssemblyIndex& index);
  void clear();
  bool isShown() const { return _result != nullptr; }
  const std::shared_ptr<const core::ThicknessResult>& result() const { return _result; }

  // Recolors without recomputing. Values in mm; thin is kept below thick.
  void setLimits(double thin, double thick);
  double thinLimit() const { return _thin; }
  double thickLimit() const { return _thick; }
  // The legend's unit.
  void setUnit(core::LengthUnit unit, int decimals);

  // The thickness under a world point of a shown part, looking along the view direction;
  // nullopt when the object is not a thickness presentation or there is no wall there.
  std::optional<double> thicknessAt(const OccHandle<AIS_InteractiveObject>& object, const gp_Pnt& world,
                                    const gp_Dir& viewDirection) const;

  // The color of a thickness under the current limits (NaN: no value, grey).
  static Quantity_Color colorOf(double thickness, double thin, double thick);

  // Diagnostics for tests.
  int presentationCount() const { return static_cast<int>(_presentations.size()); }
  bool hasLegend() const;

private:
  void updateLegend();

  OccHandle<AIS_InteractiveContext> _context;
  SceneManager& _scene;
  std::shared_ptr<const core::ThicknessResult> _result;
  std::vector<OccHandle<AIS_InteractiveObject>> _presentations;
  std::vector<core::NodeId> _parts; // the parts this view overrides
  OccHandle<AIS_ColorScale> _legend;
  double _thin = 0.0;
  double _thick = 1.0;
  core::LengthUnit _unit = core::LengthUnit::Millimeter;
  int _decimals = 2;
};

} // namespace sc::render
