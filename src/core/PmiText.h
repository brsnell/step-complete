#pragma once

#include "core/OccHandle.h"

#include <XCAFDimTolObjects_DatumObject.hxx>
#include <XCAFDimTolObjects_DimensionObject.hxx>
#include <XCAFDimTolObjects_GeomToleranceObject.hxx>

#include <string>
#include <utility>
#include <vector>

// Human-readable text for semantic PMI (GD&T), following ASME Y14.5 / ISO 1101
// conventions and their Unicode symbols (⌀ ⏥ ⌭ ⟂ ∥ ∠ ⌖ ◎ ⌯ ⌒ ⌓ ↗ ⌰ Ⓜ Ⓛ Ⓕ Ⓟ Ⓢ Ⓣ Ⓤ ...).
//
// Values are in the document unit (mm); the caller chooses the number of
// decimals when the file does not specify them. All strings are UTF-8 and
// locale-independent. Nothing here depends on Qt or on the viewer.

namespace sc::core::pmi {

using Property = std::pair<std::string, std::string>;

// --- Dimensions --------------------------------------------------------------------

// "Linear distance", "Diameter", "Radius", "Angular", "Curved distance", ...
std::string dimensionTypeName(XCAFDimTolObjects_DimensionType type);

// The dimension as it would appear on a drawing: prefix symbol (⌀, R, SR, S⌀),
// nominal value, then tolerance: "±0.05", "+0.10/−0.05" (stacked as one line),
// limits "12.70–12.75", fit class "⌀12 H7", qualifiers (min/max/avg) and
// modifiers (e.g. "(ST)", "Ⓕ", "CR"). Angles are shown in degrees with "°".
// 'decimals' is used when the dimension does not carry its own number of
// decimal places.
std::string dimensionText(const OccHandle<XCAFDimTolObjects_DimensionObject>& dimension, int decimals = 2);

// Full semantic content for the Properties panel: type, nominal, tolerance
// kind and values, limits, fit class, qualifier, modifiers, decimal places,
// direction, semantic name, and anything else the object carries. Rows that do
// not apply are omitted.
std::vector<Property> dimensionProperties(const OccHandle<XCAFDimTolObjects_DimensionObject>& dimension,
                                          int decimals = 3);

// --- Geometric tolerances ----------------------------------------------------------

// Unicode characteristic symbol: "⏥" flatness, "⌖" position, "⟂" perpendicularity, ...
std::string toleranceSymbol(XCAFDimTolObjects_GeomToleranceType type);
// "Flatness", "Position", "Profile of a surface", ...
std::string toleranceTypeName(XCAFDimTolObjects_GeomToleranceType type);

// The cells of a feature control frame, left to right:
//   [symbol] [zone: "⌀0.1 Ⓜ", "S⌀0.2", "0.05 Ⓟ 10", "0.1/25"...] [datum A Ⓜ] [datum B] ...
// 'datums' are the tolerance's datum references in order (primary, secondary,
// tertiary); each datum object's modifiers are shown with its letter.
std::vector<std::string> featureControlFrame(const OccHandle<XCAFDimTolObjects_GeomToleranceObject>& tolerance,
                                             const std::vector<OccHandle<XCAFDimTolObjects_DatumObject>>& datums,
                                             int decimals = 3);
// The frame as one line, cells separated by " | ": "⌖ | ⌀0.1 Ⓜ | A | B | C".
std::string toleranceText(const OccHandle<XCAFDimTolObjects_GeomToleranceObject>& tolerance,
                          const std::vector<OccHandle<XCAFDimTolObjects_DatumObject>>& datums, int decimals = 3);

std::vector<Property> toleranceProperties(const OccHandle<XCAFDimTolObjects_GeomToleranceObject>& tolerance,
                                          const std::vector<OccHandle<XCAFDimTolObjects_DatumObject>>& datums,
                                          int decimals = 3);

// --- Datums ------------------------------------------------------------------------

// The datum letter (e.g. "A"), or for a datum target its label (e.g. "A1").
std::string datumText(const OccHandle<XCAFDimTolObjects_DatumObject>& datum);
// "Point", "Line", "Rectangle", "Circle", "Area" ... for datum targets.
std::string datumTargetTypeName(XCAFDimTolObjects_DatumTargetType type);
std::vector<Property> datumProperties(const OccHandle<XCAFDimTolObjects_DatumObject>& datum, int decimals = 3);

} // namespace sc::core::pmi
