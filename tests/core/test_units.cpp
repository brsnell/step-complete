#include "core/Units.h"

#include <gtest/gtest.h>

#include <clocale>
#include <cmath>
#include <limits>

using namespace sc::core;

TEST(Units, ConvertsBetweenMillimetersAndOtherUnits) {
  EXPECT_DOUBLE_EQ(fromMillimeters(25.4, LengthUnit::Inch), 1.0);
  EXPECT_DOUBLE_EQ(fromMillimeters(304.8, LengthUnit::Foot), 1.0);
  EXPECT_DOUBLE_EQ(fromMillimeters(1500.0, LengthUnit::Meter), 1.5);
  EXPECT_DOUBLE_EQ(fromMillimeters(1.0, LengthUnit::Micrometer), 1000.0);
  EXPECT_DOUBLE_EQ(toMillimeters(2.0, LengthUnit::Centimeter), 20.0);
  EXPECT_DOUBLE_EQ(toMillimeters(fromMillimeters(123.456, LengthUnit::Inch), LengthUnit::Inch), 123.456);
}

TEST(Units, IdentifiesFileUnitFromScale) {
  EXPECT_EQ(lengthUnitFromScale(1.0), LengthUnit::Millimeter);
  EXPECT_EQ(lengthUnitFromScale(25.4), LengthUnit::Inch);
  EXPECT_EQ(lengthUnitFromScale(25.4 * (1.0 + 1e-9)), LengthUnit::Inch);
  EXPECT_EQ(lengthUnitFromScale(1000.0), LengthUnit::Meter);
  EXPECT_FALSE(lengthUnitFromScale(25.0).has_value());
  EXPECT_FALSE(lengthUnitFromScale(0.0).has_value());
  EXPECT_FALSE(lengthUnitFromScale(-1.0).has_value());
  EXPECT_FALSE(lengthUnitFromScale(std::numeric_limits<double>::quiet_NaN()).has_value());
}

TEST(Units, FormatsLengthWithSymbol) {
  EXPECT_EQ(formatLength(12.7, LengthUnit::Millimeter, 3), "12.700 mm");
  EXPECT_EQ(formatLength(12.7, LengthUnit::Inch, 4), "0.5000 in");
  EXPECT_EQ(formatLength(0.5, LengthUnit::Micrometer, 1), "500.0 \xC2\xB5m");
}

TEST(Units, FormatsAreaAndVolumeInSquaredAndCubedUnits) {
  EXPECT_EQ(formatArea(645.16, LengthUnit::Inch, 2), "1.00 in\xC2\xB2");
  EXPECT_EQ(formatVolume(1.0e6, LengthUnit::Centimeter, 1), "1000.0 cm\xC2\xB3");
  EXPECT_EQ(formatVolume(1.0e9, LengthUnit::Meter, 3), "1.000 m\xC2\xB3");
}

TEST(Units, FormatsAngles) {
  EXPECT_EQ(formatAngle(std::acos(-1.0) / 4.0, AngleUnit::Degree, 2), "45.00\xC2\xB0");
  EXPECT_EQ(formatAngle(1.0, AngleUnit::Radian, 3), "1.000 rad");
}

TEST(Units, NeverPrintsNegativeZero) {
  EXPECT_EQ(formatNumber(-0.0, 3), "0.000");
  EXPECT_EQ(formatNumber(-0.0004, 3), "0.000");
  EXPECT_EQ(formatNumber(-0.0005, 3), "-0.001"); // rounds away from zero; stays negative
  EXPECT_EQ(formatNumber(-0.4, 0), "0");
}

TEST(Units, RoundsAndClampsDecimals) {
  EXPECT_EQ(formatNumber(1.23456, 2), "1.23");
  EXPECT_EQ(formatNumber(2.6, 0), "3");
  EXPECT_EQ(formatNumber(1.0, -3), "1");
  EXPECT_EQ(formatNumber(1.0, 99), "1.000000000000");
}

TEST(Units, HandlesNonFiniteValues) {
  EXPECT_EQ(formatNumber(std::numeric_limits<double>::quiet_NaN(), 2), "NaN");
  EXPECT_EQ(formatNumber(std::numeric_limits<double>::infinity(), 2), "inf");
  EXPECT_EQ(formatNumber(-std::numeric_limits<double>::infinity(), 2), "-inf");
}

TEST(Units, FormattingIgnoresProcessLocale) {
  // Qt calls setlocale(LC_ALL, "") on Unix; a German locale must not turn '.' into ','.
  const char* previous = std::setlocale(LC_NUMERIC, nullptr);
  const std::string saved = previous ? previous : "C";
  if (std::setlocale(LC_NUMERIC, "de_DE.UTF-8") == nullptr) {
    GTEST_SKIP() << "de_DE.UTF-8 locale not installed";
  }
  EXPECT_EQ(formatNumber(1.5, 2), "1.50");
  std::setlocale(LC_NUMERIC, saved.c_str());
}

TEST(Units, FormatsSignificantDigits) {
  EXPECT_EQ(sc::core::formatSignificant(650000.0, 6), "650000");
  EXPECT_EQ(sc::core::formatSignificant(0.0236467481, 6), "0.0236467");
  EXPECT_EQ(sc::core::formatSignificant(1.5e-9, 6), "1.5e-09");
  EXPECT_EQ(sc::core::formatSignificant(-0.0, 6), "0");
}
