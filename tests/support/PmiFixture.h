#pragma once

#include "core/Document.h"

#include <TDF_Label.hxx>

namespace sc::test {

// A block 100 x 60 x 20 ("Block") carrying PMI the way OCCT's STEP reader
// stores it:
//  - datum A on the bottom face, plus the extra per-reference datum label "A"
//    the reader creates for each tolerance that references it,
//  - flatness 0.05 on the top face,
//  - parallelism 0.1 to A on the top face,
//  - a 20 ±0.1 height dimension between top and bottom faces, with plane,
//    text point, attachment points and a graphical presentation ("Height"),
//  - a graphical-only annotation (not semantically interpreted) on a side face,
//  - a saved view "Front" showing the flatness and the height, with a clipping plane.
struct PmiFixture {
  core::DocumentPtr document;
  TDF_Label part, top, bottom, side;
  TDF_Label datumA, datumARef, flatness, parallelism, height, label, view;
};

PmiFixture buildPmiFixture();

} // namespace sc::test
