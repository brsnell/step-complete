#pragma once

#include "core/Document.h"

#include <string>

namespace sc::test {

// A small product structure covering the cases the viewer must handle:
//
//   Fixture (assembly)
//   +-- Base plate           part, red, 100 x 60 x 10 mm box at the origin
//   +-- Pin pair #1          assembly instance at (10, 10, 10)
//   |   +-- Pin Ø6           part (no color), cylinder r=3 h=20 at (0, 0, 0)
//   |   +-- Pin Ø6           second instance at (20, 0, 0), instance color green
//   +-- Pin pair #2          assembly instance at (50, 10, 10), hidden if requested
//       +-- (same two pins)
//
// 8 nodes, 5 part instances, 2 unique parts.
//
// withHiddenInstance: OCCT 7.6's STEP *writer* crashes in WriteColors on hidden
// component instances, so STEP round-trip tests build the fixture without it.
core::DocumentPtr buildFixtureAssembly(bool withHiddenInstance = true);

// Writes the document as STEP AP214 in the given unit ("MM", "INCH", ...).
// Returns false on failure.
bool writeStep(const core::DocumentPtr& document, const std::string& utf8Path, const char* unit = "MM");

// A unique path in the test temp directory.
std::string tempPath(const std::string& fileName);

} // namespace sc::test
