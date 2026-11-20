#pragma once

#include "core/Measurement.h"
#include "core/OccHandle.h"

#include <AIS_InteractiveObject.hxx>

namespace sc::render {

// Draws a measurement's witness geometry (lines, arcs, point markers and a
// value label) on top of the model, so users can see exactly what was
// measured. Not selectable: picks pass through it to the model.
//
// Several witnesses can be on screen at once. The emphasized one (the result
// the user is looking at) is drawn in full orange; the others are drawn
// thinner and paler so the view stays readable.
class WitnessPresentation : public AIS_InteractiveObject {
  DEFINE_STANDARD_RTTI_INLINE(WitnessPresentation, AIS_InteractiveObject)

public:
  explicit WitnessPresentation(core::Witness witness, double devicePixelRatio = 1.0);

  const core::Witness& witness() const { return m_witness; }

  // Takes effect on the next Redisplay.
  void setEmphasized(bool emphasized) { m_emphasized = emphasized; }
  bool isEmphasized() const { return m_emphasized; }

  bool AcceptDisplayMode(const int mode) const override { return mode == 0; }

protected:
  void Compute(const OccHandle<PrsMgr_PresentationManager>& manager, const OccHandle<Prs3d_Presentation>& prs,
               const int mode) override;
  void ComputeSelection(const OccHandle<SelectMgr_Selection>&, const int) override {}

private:
  core::Witness m_witness;
  double m_dpr;
  bool m_emphasized = true;
};

} // namespace sc::render
