#pragma once

#include "core/Measurement.h"
#include "core/OccHandle.h"

#include <AIS_InteractiveObject.hxx>

namespace sc::render {

// Draws a measurement's witness geometry (lines, arcs, point markers and a
// value label) on top of the model, so users can see exactly what was
// measured. Not selectable: picks pass through it to the model.
class WitnessPresentation : public AIS_InteractiveObject {
  DEFINE_STANDARD_RTTI_INLINE(WitnessPresentation, AIS_InteractiveObject)

public:
  explicit WitnessPresentation(core::Witness witness, double devicePixelRatio = 1.0);

  const core::Witness& witness() const { return m_witness; }

  bool AcceptDisplayMode(const int mode) const override { return mode == 0; }

protected:
  void Compute(const OccHandle<PrsMgr_PresentationManager>& manager, const OccHandle<Prs3d_Presentation>& prs,
               const int mode) override;
  void ComputeSelection(const OccHandle<SelectMgr_Selection>&, const int) override {}

private:
  core::Witness m_witness;
  double m_dpr;
};

} // namespace sc::render
