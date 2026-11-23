#pragma once

#include "core/AssemblyIndex.h"
#include "core/Comparison.h"
#include "core/OccHandle.h"
#include "core/Units.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <Bnd_Box.hxx>
#include <Quantity_Color.hxx>

#include <map>
#include <memory>
#include <string>
#include <vector>

class AIS_ColorScale;

namespace sc::render {

class SceneManager;

// The model comparison view (docs/designs/model-comparison.md): the current revision's parts
// colored by their change, the changed ones by deviation (green within the tolerance, red where
// material was added, blue where it was taken away) with a legend, and the removed parts as red
// see-through ghosts from the other revision. Parts are swapped through SceneManager overrides,
// so selection, hiding and sections keep working. Never redraws; the caller invalidates.
class ComparisonScene {
public:
  ComparisonScene(const OccHandle<AIS_InteractiveContext>& context, SceneManager& scene);
  ~ComparisonScene();

  ComparisonScene(const ComparisonScene&) = delete;
  ComparisonScene& operator=(const ComparisonScene&) = delete;

  void show(std::shared_ptr<const core::ComparisonResult> result, const core::AssemblyIndex& current,
            std::shared_ptr<const core::AssemblyIndex> other, double tolerance);
  void clear();
  bool isShown() const { return _result != nullptr; }
  const std::shared_ptr<const core::ComparisonResult>& result() const { return _result; }
  const std::shared_ptr<const core::AssemblyIndex>& other() const { return _other; }

  void setUnit(core::LengthUnit unit, int decimals);

  // World box of a removed part's ghost (void when the change has none), to zoom to it.
  Bnd_Box ghostBox(int changeIndex) const;

  // Colors, for the legend and the panel.
  static Quantity_Color colorOf(core::ChangeKind kind);
  static Quantity_Color deviationColor(double value, double tolerance, double range);

  // Diagnostics for tests.
  int overrideCount() const { return static_cast<int>(_parts.size()); }
  int ghostCount() const { return static_cast<int>(_ghosts.size()); }
  bool hasLegend() const;

private:
  void updateLegend();

  OccHandle<AIS_InteractiveContext> _context;
  SceneManager& _scene;
  std::shared_ptr<const core::ComparisonResult> _result;
  std::shared_ptr<const core::AssemblyIndex> _other;
  std::vector<core::NodeId> _parts;                        // overridden current parts
  std::map<int, OccHandle<AIS_InteractiveObject>> _ghosts; // by change index
  OccHandle<AIS_ColorScale> _legend;
  double _tolerance = 0.01;
  double _range = 1.0; // legend half-width, mm
  core::LengthUnit _unit = core::LengthUnit::Millimeter;
  int _decimals = 3;
};

} // namespace sc::render
