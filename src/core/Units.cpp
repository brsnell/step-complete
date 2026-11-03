#include "core/Units.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <locale>
#include <sstream>

namespace sc::core {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct LengthUnitInfo {
  LengthUnit unit;
  double mmPerUnit;
  std::string_view symbol;
  std::string_view name;
};

constexpr std::array<LengthUnitInfo, 6> kLengthUnits{{
    {LengthUnit::Micrometer, 0.001, "\xC2\xB5m", "Micrometers"}, // µm
    {LengthUnit::Millimeter, 1.0, "mm", "Millimeters"},
    {LengthUnit::Centimeter, 10.0, "cm", "Centimeters"},
    {LengthUnit::Meter, 1000.0, "m", "Meters"},
    {LengthUnit::Inch, 25.4, "in", "Inches"},
    {LengthUnit::Foot, 304.8, "ft", "Feet"},
}};

const LengthUnitInfo& info(LengthUnit unit) {
  for (const auto& entry : kLengthUnits) {
    if (entry.unit == unit) {
      return entry;
    }
  }
  return kLengthUnits[1]; // unreachable for valid enum values
}

} // namespace

double millimetersPer(LengthUnit unit) {
  return info(unit).mmPerUnit;
}

double fromMillimeters(double mm, LengthUnit unit) {
  return mm / millimetersPer(unit);
}

double toMillimeters(double value, LengthUnit unit) {
  return value * millimetersPer(unit);
}

std::string_view symbol(LengthUnit unit) {
  return info(unit).symbol;
}

std::string_view symbol(AngleUnit unit) {
  return unit == AngleUnit::Degree ? std::string_view("\xC2\xB0") : std::string_view("rad");
}

std::string_view displayName(LengthUnit unit) {
  return info(unit).name;
}

std::optional<LengthUnit> lengthUnitFromScale(double mmPerUnit) {
  if (!(mmPerUnit > 0.0) || !std::isfinite(mmPerUnit)) {
    return std::nullopt;
  }
  constexpr double kRelTol = 1e-6;
  for (const auto& entry : kLengthUnits) {
    if (std::abs(mmPerUnit - entry.mmPerUnit) <= kRelTol * entry.mmPerUnit) {
      return entry.unit;
    }
  }
  return std::nullopt;
}

std::string formatNumber(double value, int decimals) {
  if (std::isnan(value)) {
    return "NaN";
  }
  if (std::isinf(value)) {
    return value > 0 ? "inf" : "-inf";
  }
  decimals = std::clamp(decimals, 0, 12);

  // Collapse values that round to zero (including -0.0) to +0 so we never print "-0.000".
  const double scale = std::pow(10.0, decimals);
  if (std::abs(value) * scale < 0.5) {
    value = 0.0;
  }

  std::ostringstream out;
  out.imbue(std::locale::classic());
  out.setf(std::ios::fixed);
  out.precision(decimals);
  out << value;
  return out.str();
}

std::string formatSignificant(double value, int digits) {
  if (std::isnan(value) || std::isinf(value)) {
    return formatNumber(value, 0);
  }
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out.precision(std::clamp(digits, 1, 17));
  out << (value == 0.0 ? 0.0 : value); // no "-0"
  return out.str();
}

std::string formatLength(double mm, LengthUnit unit, int decimals) {
  std::string text = formatNumber(fromMillimeters(mm, unit), decimals);
  text += ' ';
  text += symbol(unit);
  return text;
}

std::string formatArea(double mm2, LengthUnit unit, int decimals) {
  const double factor = millimetersPer(unit);
  std::string text = formatNumber(mm2 / (factor * factor), decimals);
  text += ' ';
  text += symbol(unit);
  text += "\xC2\xB2"; // ²
  return text;
}

std::string formatVolume(double mm3, LengthUnit unit, int decimals) {
  const double factor = millimetersPer(unit);
  std::string text = formatNumber(mm3 / (factor * factor * factor), decimals);
  text += ' ';
  text += symbol(unit);
  text += "\xC2\xB3"; // ³
  return text;
}

std::string formatAngle(double radians, AngleUnit unit, int decimals) {
  if (unit == AngleUnit::Degree) {
    return formatNumber(radians * 180.0 / kPi, decimals) + std::string(symbol(unit));
  }
  return formatNumber(radians, decimals) + " " + std::string(symbol(unit));
}

} // namespace sc::core
