#pragma once

#include "core/OccHandle.h"
#include "core/PmiModel.h"

#include <AIS_InteractiveObject.hxx>
#include <gp_Pnt.hxx>

#include <string>
#include <utility>
#include <vector>

namespace sc::render {

// The semantic-only look of one PMI item at one placement, drawn in screen space at a
// constant pixel size (zoom and rotation persistent) next to a world-space anchor:
//  - geometric tolerances as a feature control frame, a row of boxed cells,
//  - datums as a datum flag, a boxed letter on a short stem,
//  - dimensions, datum targets and anything else as the value text in a box.
// Labels that share an anchor are stacked downwards by `stackIndex` rows so they do not
// overlap. The box starts at the anchor and is vertically centered on its row. A further
// pixel offset (setOffset, set by PmiScene::declutter) moves the box away from the anchor;
// a displaced box is joined to the anchor by a connector line.
//
// Local coordinates are device pixels: x to the right, y up, origin at the anchor.
// Selectable as a whole (mode 0); pmiId() says which PMI item it shows.
class PmiLabel : public AIS_InteractiveObject {
  DEFINE_STANDARD_RTTI_INLINE(PmiLabel, AIS_InteractiveObject)

public:
  PmiLabel(const core::PmiItem& item, const gp_Pnt& worldAnchor, int stackIndex, double devicePixelRatio);

  core::PmiId pmiId() const { return _id; }
  const gp_Pnt& anchor() const { return _anchor; }
  int stackIndex() const { return _stackIndex; }

  // The box in device pixels relative to the anchor (x right, y up): the first cell
  // starts at boxLeft() (after a stem, if any) and the box is boxHeight() tall, centered
  // on the stack row's center, `stackIndex * stackPitch` below the anchor.
  double boxLeft() const { return _boxLeft; }
  double boxWidth() const { return _boxWidth; }
  double boxHeight() const { return _rowHeight; }
  double centerY() const { return _centerY; }

  // Moves the box by (dx, dy) device pixels (y up) from its stack row. The caller redisplays.
  void setOffset(double dx, double dy);
  double offsetX() const { return _offsetX; }
  double offsetY() const { return _offsetY; }

  // Horizontal extent of each cell, in device pixels from the anchor.
  std::vector<std::pair<double, double>> cellExtents() const;

  // Height of one row and the distance between stacked rows, in device pixels.
  static double rowHeight(double devicePixelRatio);
  static double stackPitch(double devicePixelRatio);

  bool AcceptDisplayMode(const int mode) const override { return mode == 0; }

protected:
  void Compute(const OccHandle<PrsMgr_PresentationManager>& manager, const OccHandle<Prs3d_Presentation>& prs,
               const int mode) override;
  void ComputeSelection(const OccHandle<SelectMgr_Selection>& selection, const int mode) override;

private:
  struct Cell {
    std::string text;
    double x0 = 0.0; // pixels from the anchor
    double x1 = 0.0;
  };
  void layout(const core::PmiItem& item);

  core::PmiId _id;
  gp_Pnt _anchor;
  int _stackIndex;
  double _dpr;
  double _rowHeight = 0.0;
  double _stem = 0.0; // datum flags
  double _boxLeft = 0.0;
  double _boxWidth = 0.0;
  double _centerY = 0.0;
  double _offsetX = 0.0;
  double _offsetY = 0.0;
  std::vector<Cell> _cells;
};

} // namespace sc::render
