#pragma once

#include <optional>
#include <string>
#include <string_view>

// Unit conversion and formatting for displayed values.
//
// Convention: all geometry inside the application is in millimeters (OCCT's
// default once a document's length unit is set). Conversion to the user's unit
// happens only when a value is shown.
//
// Formatting is locale-independent ('.' decimal separator) on purpose: values
// are copied into spreadsheets, reports, and bug reports.

namespace sc::core {

enum class LengthUnit {
  Micrometer,
  Millimeter,
  Centimeter,
  Meter,
  Inch,
  Foot,
};

enum class AngleUnit {
  Degree,
  Radian,
};

// Millimeters per one unit, e.g. 25.4 for Inch.
double millimetersPer(LengthUnit unit);

double fromMillimeters(double mm, LengthUnit unit);
double toMillimeters(double value, LengthUnit unit);

std::string_view symbol(LengthUnit unit);
std::string_view symbol(AngleUnit unit);
std::string_view displayName(LengthUnit unit);

// Maps a scale factor (millimeters per file unit) to a known unit, within a
// relative tolerance. Used to name the length unit declared in a STEP file.
std::optional<LengthUnit> lengthUnitFromScale(double mmPerUnit);

// Fixed-point formatting with the unit symbol: "12.700 mm", "0.5000 in".
// Negative zero is printed as zero. 'decimals' is clamped to [0, 12].
std::string formatNumber(double value, int decimals);
// Significant-digit formatting for quantities spanning many orders of
// magnitude (e.g. moments of inertia): "650000", "0.0237683", "1.5e-09".
std::string formatSignificant(double value, int digits);
std::string formatLength(double mm, LengthUnit unit, int decimals);
std::string formatArea(double mm2, LengthUnit unit, int decimals);     // "mm²"
std::string formatVolume(double mm3, LengthUnit unit, int decimals);   // "mm³"
std::string formatAngle(double radians, AngleUnit unit, int decimals); // "45.00°"

} // namespace sc::core
