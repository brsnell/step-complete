#include "core/PmiText.h"

#include <TCollection_HAsciiString.hxx>
#include <gp_Dir.hxx>

#include <gtest/gtest.h>

#include <clocale>
#include <cmath>
#include <locale>
#include <string>
#include <vector>

using namespace sc::core::pmi;
using sc::OccHandle;

namespace {

using Dim = OccHandle<XCAFDimTolObjects_DimensionObject>;
using Tol = OccHandle<XCAFDimTolObjects_GeomToleranceObject>;
using Datum = OccHandle<XCAFDimTolObjects_DatumObject>;

Dim makeDimension(XCAFDimTolObjects_DimensionType type, double value) {
  Dim dim = new XCAFDimTolObjects_DimensionObject();
  dim->SetType(type);
  dim->SetValue(value);
  return dim;
}

Tol makeTolerance(XCAFDimTolObjects_GeomToleranceType type, double value) {
  Tol tol = new XCAFDimTolObjects_GeomToleranceObject();
  tol->SetType(type);
  tol->SetValue(value);
  return tol;
}

Datum makeDatum(const char* name) {
  Datum datum = new XCAFDimTolObjects_DatumObject();
  datum->SetName(new TCollection_HAsciiString(name));
  return datum;
}

bool hasRow(const std::vector<Property>& rows, const std::string& key, const std::string& value) {
  for (const Property& row : rows) {
    if (row.first == key && row.second == value) {
      return true;
    }
  }
  return false;
}

bool hasKey(const std::vector<Property>& rows, const std::string& key) {
  for (const Property& row : rows) {
    if (row.first == key) {
      return true;
    }
  }
  return false;
}

// Position tolerance ⌀0.1 Ⓜ | A | B Ⓜ | C used by several tests.
struct PositionFrame {
  Tol tol;
  std::vector<Datum> datums;
};

PositionFrame makePositionFrame() {
  PositionFrame frame;
  frame.tol = makeTolerance(XCAFDimTolObjects_GeomToleranceType_Position, 0.1);
  frame.tol->SetTypeOfValue(XCAFDimTolObjects_GeomToleranceTypeValue_Diameter);
  frame.tol->SetMaterialRequirementModifier(XCAFDimTolObjects_GeomToleranceMatReqModif_M);
  Datum a = makeDatum("A");
  Datum b = makeDatum("B");
  b->AddModifier(XCAFDimTolObjects_DatumSingleModif_MaximumMaterialRequirement);
  Datum c = makeDatum("C");
  frame.datums = {a, b, c};
  return frame;
}

} // namespace

// --- Dimensions ---------------------------------------------------------------------

TEST(PmiText, DiameterWithSymmetricTolerance) {
  Dim dim = makeDimension(XCAFDimTolObjects_DimensionType_Size_Diameter, 12.7);
  dim->SetUpperTolValue(0.05);
  dim->SetLowerTolValue(-0.05);
  EXPECT_EQ(dimensionText(dim), "⌀12.70 ±0.05");
}

TEST(PmiText, DiameterWithAsymmetricTolerance) {
  Dim dim = makeDimension(XCAFDimTolObjects_DimensionType_Size_Diameter, 12.7);
  dim->SetUpperTolValue(0.10);
  dim->SetLowerTolValue(-0.05);
  EXPECT_EQ(dimensionText(dim), "⌀12.70 +0.10/−0.05");
  EXPECT_EQ(dimensionText(dim).find("-0"), std::string::npos) << "minus must be U+2212";
}

TEST(PmiText, ZeroLowerDeviationHasNoSign) {
  Dim dim = makeDimension(XCAFDimTolObjects_DimensionType_Size_Diameter, 20.0);
  dim->SetUpperTolValue(0.02);
  dim->SetLowerTolValue(0.0);
  EXPECT_EQ(dimensionText(dim), "⌀20.00 +0.02/0.00");
}

TEST(PmiText, DimensionWithLimits) {
  Dim dim = makeDimension(XCAFDimTolObjects_DimensionType_Size_Diameter, 12.7);
  dim->SetLowerBound(12.70);
  dim->SetUpperBound(12.75);
  ASSERT_TRUE(dim->IsDimWithRange());
  EXPECT_EQ(dimensionText(dim), "⌀12.70–12.75");
  EXPECT_TRUE(hasRow(dimensionProperties(dim), "Lower limit", "12.700 mm"));
  EXPECT_TRUE(hasRow(dimensionProperties(dim), "Upper limit", "12.750 mm"));
  EXPECT_TRUE(hasRow(dimensionProperties(dim), "Tolerance kind", "Limits"));
}

TEST(PmiText, FitClassForHoleAndShaft) {
  Dim hole = makeDimension(XCAFDimTolObjects_DimensionType_Size_Diameter, 12.0);
  hole->SetClassOfTolerance(true, XCAFDimTolObjects_DimensionFormVariance_H,
                            XCAFDimTolObjects_DimensionGrade_IT7);
  EXPECT_EQ(dimensionText(hole), "⌀12 H7");

  Dim shaft = makeDimension(XCAFDimTolObjects_DimensionType_Size_Diameter, 12.0);
  shaft->SetClassOfTolerance(false, XCAFDimTolObjects_DimensionFormVariance_JS,
                             XCAFDimTolObjects_DimensionGrade_IT6);
  EXPECT_EQ(dimensionText(shaft), "⌀12 js6");

  const auto rows = dimensionProperties(hole);
  EXPECT_TRUE(hasRow(rows, "Fit class", "H7"));
  EXPECT_TRUE(hasRow(rows, "Applies to", "Hole (internal feature)"));
  EXPECT_TRUE(hasRow(rows, "Tolerance kind", "Fit class"));
}

TEST(PmiText, RadiusAndSizePrefixes) {
  EXPECT_EQ(dimensionText(makeDimension(XCAFDimTolObjects_DimensionType_Size_Radius, 5.0)), "R5.00");
  EXPECT_EQ(dimensionText(makeDimension(XCAFDimTolObjects_DimensionType_Size_SphericalRadius, 5.0)),
            "SR5.00");
  EXPECT_EQ(dimensionText(makeDimension(XCAFDimTolObjects_DimensionType_Size_SphericalDiameter, 8.0)),
            "S⌀8.00");
  EXPECT_EQ(dimensionText(makeDimension(XCAFDimTolObjects_DimensionType_Location_LinearDistance, 25.4)),
            "25.40");
}

// OCCT's STEP reader converts angular dimensions to degrees (convertAngleValue in
// STEPCAFControl_Reader), so the stored values are degrees.
TEST(PmiText, AngularDimensionsAreInDegrees) {
  Dim dim = makeDimension(XCAFDimTolObjects_DimensionType_Location_Angular, 30.0);
  EXPECT_EQ(dimensionText(dim), "30.00°");
  dim->SetUpperTolValue(0.5);
  dim->SetLowerTolValue(-0.5);
  EXPECT_EQ(dimensionText(dim), "30.00° ±0.50°");
  Dim size = makeDimension(XCAFDimTolObjects_DimensionType_Size_Angular, 45.0);
  EXPECT_EQ(dimensionText(size, 1), "45.0°");
}

// The STEP reader stores (nominal, |lower deviation|, upper deviation): a positive
// stored lower value still means "minus".
TEST(PmiText, LowerDeviationIsStoredAsMagnitudeByTheReader) {
  Dim dim = makeDimension(XCAFDimTolObjects_DimensionType_Size_Diameter, 35.0);
  dim->SetUpperTolValue(0.0);
  dim->SetLowerTolValue(0.2); // as read from TOLERANCE_VALUE(-0.2, 0.0)
  EXPECT_EQ(dimensionText(dim), "⌀35.00 0.00/−0.20");
  dim->SetUpperTolValue(0.15);
  dim->SetLowerTolValue(0.15);
  EXPECT_EQ(dimensionText(dim), "⌀35.00 ±0.15");
}

TEST(PmiText, DecimalsFromObjectOrFallback) {
  Dim dim = makeDimension(XCAFDimTolObjects_DimensionType_Size_Diameter, 12.7);
  EXPECT_EQ(dimensionText(dim), "⌀12.70");
  EXPECT_EQ(dimensionText(dim, 3), "⌀12.700");
  EXPECT_EQ(dimensionText(dim, 0), "⌀13");
  dim->SetNbOfDecimalPlaces(2, 4);
  EXPECT_EQ(dimensionText(dim), "⌀12.7000");
  EXPECT_EQ(dimensionText(dim, 1), "⌀12.7000") << "the object's own decimals win over the fallback";
  EXPECT_TRUE(hasRow(dimensionProperties(dim), "Decimal places", "2.4"));
}

TEST(PmiText, QualifiersAndModifiers) {
  Dim dim = makeDimension(XCAFDimTolObjects_DimensionType_Size_Diameter, 10.0);
  dim->SetQualifier(XCAFDimTolObjects_DimensionQualifier_Max);
  dim->AddModifier(XCAFDimTolObjects_DimensionModif_StatisticalTolerance);
  dim->AddModifier(XCAFDimTolObjects_DimensionModif_FreeStateCondition);
  EXPECT_EQ(dimensionText(dim), "⌀10.00 max (ST) Ⓕ");

  Dim radius = makeDimension(XCAFDimTolObjects_DimensionType_Size_Radius, 3.0);
  radius->AddModifier(XCAFDimTolObjects_DimensionModif_ControlledRadius);
  EXPECT_EQ(dimensionText(radius), "CR3.00");

  Dim square = makeDimension(XCAFDimTolObjects_DimensionType_Size_Thickness, 10.0);
  square->AddModifier(XCAFDimTolObjects_DimensionModif_Square);
  EXPECT_EQ(dimensionText(square), "◻10.00");

  Dim cf = makeDimension(XCAFDimTolObjects_DimensionType_Size_Diameter, 6.0);
  cf->AddModifier(XCAFDimTolObjects_DimensionModif_ContinuousFeature);
  EXPECT_EQ(dimensionText(cf), "⌀6.00 CF");
}

TEST(PmiText, DimensionProperties) {
  Dim dim = makeDimension(XCAFDimTolObjects_DimensionType_Size_Diameter, 12.7);
  dim->SetUpperTolValue(0.10);
  dim->SetLowerTolValue(-0.05);
  dim->SetQualifier(XCAFDimTolObjects_DimensionQualifier_Min);
  dim->AddModifier(XCAFDimTolObjects_DimensionModif_StatisticalTolerance);
  dim->SetSemanticName(new TCollection_HAsciiString("hole diameter"));
  dim->SetDirection(gp_Dir(0, 0, 1));

  const auto rows = dimensionProperties(dim);
  EXPECT_TRUE(hasRow(rows, "Type", "Diameter"));
  EXPECT_TRUE(hasRow(rows, "Text", "⌀12.700 +0.100/−0.050 min (ST)"));
  EXPECT_TRUE(hasRow(rows, "Semantic name", "hole diameter"));
  EXPECT_TRUE(hasRow(rows, "Nominal", "12.700 mm"));
  EXPECT_TRUE(hasRow(rows, "Tolerance kind", "Asymmetric (+/−)"));
  EXPECT_TRUE(hasRow(rows, "Upper deviation", "+0.100 mm"));
  EXPECT_TRUE(hasRow(rows, "Lower deviation", "−0.050 mm"));
  EXPECT_TRUE(hasRow(rows, "Qualifier", "Minimum"));
  EXPECT_TRUE(hasRow(rows, "Modifiers", "Statistical tolerance ((ST))"));
  EXPECT_TRUE(hasRow(rows, "Direction", "(0, 0, 1)"));
  EXPECT_FALSE(hasKey(rows, "Fit class"));
  EXPECT_FALSE(hasKey(rows, "Lower limit"));
  EXPECT_FALSE(hasKey(rows, "Decimal places"));
}

TEST(PmiText, DimensionTypeNames) {
  EXPECT_EQ(dimensionTypeName(XCAFDimTolObjects_DimensionType_Location_LinearDistance), "Linear distance");
  EXPECT_EQ(dimensionTypeName(XCAFDimTolObjects_DimensionType_Size_Diameter), "Diameter");
  EXPECT_EQ(dimensionTypeName(XCAFDimTolObjects_DimensionType_Size_Radius), "Radius");
  EXPECT_EQ(dimensionTypeName(XCAFDimTolObjects_DimensionType_Location_Angular), "Angular");
  EXPECT_EQ(dimensionTypeName(XCAFDimTolObjects_DimensionType_Location_CurvedDistance), "Curved distance");
  EXPECT_EQ(dimensionTypeName(static_cast<XCAFDimTolObjects_DimensionType>(9999)), "Dimension (type 9999)");
}

TEST(PmiText, NullAndDefaultObjectsAreSafe) {
  EXPECT_EQ(dimensionText(Dim()), "");
  EXPECT_TRUE(dimensionProperties(Dim()).empty());
  EXPECT_EQ(toleranceText(Tol(), {}), "");
  EXPECT_TRUE(featureControlFrame(Tol(), {}).empty());
  EXPECT_TRUE(toleranceProperties(Tol(), {}).empty());
  EXPECT_EQ(datumText(Datum()), "");
  EXPECT_TRUE(datumProperties(Datum()).empty());

  Dim dim = new XCAFDimTolObjects_DimensionObject();
  EXPECT_NO_FATAL_FAILURE((void)dimensionText(dim));
  EXPECT_NO_FATAL_FAILURE((void)dimensionProperties(dim));
  Tol tol = new XCAFDimTolObjects_GeomToleranceObject();
  EXPECT_NO_FATAL_FAILURE((void)toleranceText(tol, {}));
  EXPECT_NO_FATAL_FAILURE((void)toleranceProperties(tol, {}));
  Datum datum = new XCAFDimTolObjects_DatumObject();
  EXPECT_NO_FATAL_FAILURE((void)datumText(datum));
  EXPECT_NO_FATAL_FAILURE((void)datumProperties(datum));
}

// --- Geometric tolerances -----------------------------------------------------------

TEST(PmiText, FlatnessFrame) {
  Tol tol = makeTolerance(XCAFDimTolObjects_GeomToleranceType_Flatness, 0.05);
  EXPECT_EQ(toleranceText(tol, {}), "⏥ | 0.05");
  const auto cells = featureControlFrame(tol, {});
  ASSERT_EQ(cells.size(), 2u);
  EXPECT_EQ(cells[0], "⏥");
  EXPECT_EQ(cells[1], "0.05");
}

TEST(PmiText, PositionWithDiameterZoneMmcAndDatums) {
  const PositionFrame frame = makePositionFrame();
  EXPECT_EQ(toleranceText(frame.tol, frame.datums), "⌖ | ⌀0.1 Ⓜ | A | B Ⓜ | C");
  const auto cells = featureControlFrame(frame.tol, frame.datums);
  ASSERT_EQ(cells.size(), 5u);
  EXPECT_EQ(cells[1], "⌀0.1 Ⓜ");
  EXPECT_EQ(cells[3], "B Ⓜ");
}

TEST(PmiText, ProjectedToleranceZone) {
  Tol tol = makeTolerance(XCAFDimTolObjects_GeomToleranceType_Perpendicularity, 0.05);
  tol->SetZoneModifier(XCAFDimTolObjects_GeomToleranceZoneModif_Projected);
  tol->SetValueOfZoneModifier(10.0);
  Datum a = makeDatum("A");
  EXPECT_EQ(toleranceText(tol, {a}), "⟂ | 0.05 Ⓟ 10 | A");

  Tol noValue = makeTolerance(XCAFDimTolObjects_GeomToleranceType_Position, 0.2);
  noValue->SetZoneModifier(XCAFDimTolObjects_GeomToleranceZoneModif_Projected);
  EXPECT_EQ(toleranceText(noValue, {}), "⌖ | 0.2 Ⓟ");
}

TEST(PmiText, ProfileOfSurfaceWithoutDatums) {
  Tol tol = makeTolerance(XCAFDimTolObjects_GeomToleranceType_ProfileOfSurface, 0.2);
  EXPECT_EQ(toleranceText(tol, {}), "⌓ | 0.2");
}

TEST(PmiText, SphericalZonePerUnitLengthAndOtherModifiers) {
  Tol spherical = makeTolerance(XCAFDimTolObjects_GeomToleranceType_Position, 0.2);
  spherical->SetTypeOfValue(XCAFDimTolObjects_GeomToleranceTypeValue_SphericalDiameter);
  EXPECT_EQ(toleranceText(spherical, {}), "⌖ | S⌀0.2");

  Tol perLength = makeTolerance(XCAFDimTolObjects_GeomToleranceType_Flatness, 0.1);
  perLength->SetMaxValueModifier(25.0);
  EXPECT_EQ(toleranceText(perLength, {}), "⏥ | 0.1/25");

  Tol flags = makeTolerance(XCAFDimTolObjects_GeomToleranceType_Flatness, 0.1);
  flags->SetMaterialRequirementModifier(XCAFDimTolObjects_GeomToleranceMatReqModif_L);
  flags->AddModifier(XCAFDimTolObjects_GeomToleranceModif_Free_State);
  flags->AddModifier(XCAFDimTolObjects_GeomToleranceModif_Tangent_Plane);
  flags->AddModifier(XCAFDimTolObjects_GeomToleranceModif_Statistical_Tolerance);
  flags->AddModifier(XCAFDimTolObjects_GeomToleranceModif_Least_Material_Requirement); // duplicate of L
  EXPECT_EQ(toleranceText(flags, {}), "⏥ | 0.1 Ⓛ Ⓕ Ⓣ (ST)");

  Tol unequal = makeTolerance(XCAFDimTolObjects_GeomToleranceType_ProfileOfSurface, 0.3);
  unequal->SetZoneModifier(XCAFDimTolObjects_GeomToleranceZoneModif_NonUniform);
  unequal->SetValueOfZoneModifier(0.1);
  EXPECT_EQ(toleranceText(unequal, {}), "⌓ | 0.3 Ⓤ 0.1");
}

TEST(PmiText, EveryToleranceTypeHasSymbolAndName) {
  using T = XCAFDimTolObjects_GeomToleranceType;
  const std::pair<T, const char*> expected[] = {
      {XCAFDimTolObjects_GeomToleranceType_Straightness, "⏤"},
      {XCAFDimTolObjects_GeomToleranceType_Flatness, "⏥"},
      {XCAFDimTolObjects_GeomToleranceType_CircularityOrRoundness, "○"},
      {XCAFDimTolObjects_GeomToleranceType_Cylindricity, "⌭"},
      {XCAFDimTolObjects_GeomToleranceType_ProfileOfLine, "⌒"},
      {XCAFDimTolObjects_GeomToleranceType_ProfileOfSurface, "⌓"},
      {XCAFDimTolObjects_GeomToleranceType_Angularity, "∠"},
      {XCAFDimTolObjects_GeomToleranceType_Perpendicularity, "⟂"},
      {XCAFDimTolObjects_GeomToleranceType_Parallelism, "∥"},
      {XCAFDimTolObjects_GeomToleranceType_Position, "⌖"},
      {XCAFDimTolObjects_GeomToleranceType_Concentricity, "◎"},
      {XCAFDimTolObjects_GeomToleranceType_Coaxiality, "◎"},
      {XCAFDimTolObjects_GeomToleranceType_Symmetry, "⌯"},
      {XCAFDimTolObjects_GeomToleranceType_CircularRunout, "↗"},
      {XCAFDimTolObjects_GeomToleranceType_TotalRunout, "⌰"},
  };
  for (const auto& [type, symbol] : expected) {
    EXPECT_EQ(toleranceSymbol(type), symbol) << toleranceTypeName(type);
    EXPECT_FALSE(toleranceTypeName(type).empty());
  }
  EXPECT_EQ(toleranceTypeName(XCAFDimTolObjects_GeomToleranceType_ProfileOfSurface), "Profile of a surface");
  EXPECT_EQ(toleranceTypeName(XCAFDimTolObjects_GeomToleranceType_Flatness), "Flatness");
  EXPECT_EQ(toleranceSymbol(XCAFDimTolObjects_GeomToleranceType_None), "");
}

TEST(PmiText, ToleranceProperties) {
  const PositionFrame frame = makePositionFrame();
  frame.tol->SetZoneModifier(XCAFDimTolObjects_GeomToleranceZoneModif_Projected);
  frame.tol->SetValueOfZoneModifier(5.0);
  frame.tol->AddModifier(XCAFDimTolObjects_GeomToleranceModif_Statistical_Tolerance);
  frame.tol->SetSemanticName(new TCollection_HAsciiString("hole position"));

  const auto rows = toleranceProperties(frame.tol, frame.datums);
  EXPECT_TRUE(hasRow(rows, "Type", "Position"));
  EXPECT_TRUE(hasRow(rows, "Symbol", "⌖"));
  EXPECT_TRUE(hasRow(rows, "Semantic name", "hole position"));
  EXPECT_TRUE(hasRow(rows, "Tolerance value", "0.1 mm"));
  EXPECT_TRUE(hasRow(rows, "Zone shape", "Cylindrical (diameter)"));
  EXPECT_TRUE(hasRow(rows, "Material requirement", "Maximum material condition (M)"));
  EXPECT_TRUE(hasRow(rows, "Zone modifier", "Projected tolerance zone (P): 5 mm"));
  EXPECT_TRUE(hasRow(rows, "Modifiers", "Statistical tolerance ((ST))"));
  EXPECT_TRUE(hasRow(rows, "Datum 1 (primary)", "A"));
  EXPECT_TRUE(hasRow(rows, "Datum 2 (secondary)", "B Ⓜ"));
  EXPECT_TRUE(hasRow(rows, "Datum 3 (tertiary)", "C"));
  EXPECT_FALSE(hasKey(rows, "Per unit length"));

  const auto flat =
      toleranceProperties(makeTolerance(XCAFDimTolObjects_GeomToleranceType_Flatness, 0.05), {});
  EXPECT_TRUE(hasRow(flat, "Text", "⏥ | 0.05"));
  EXPECT_FALSE(hasKey(flat, "Zone shape"));
  EXPECT_FALSE(hasKey(flat, "Material requirement"));
  EXPECT_FALSE(hasKey(flat, "Datum 1 (primary)"));
}

// --- Datums -------------------------------------------------------------------------

TEST(PmiText, DatumText) {
  EXPECT_EQ(datumText(makeDatum("A")), "A");
}

TEST(PmiText, DatumTargetTextAndType) {
  Datum target = makeDatum("A");
  target->IsDatumTarget(true);
  target->SetDatumTargetNumber(1);
  target->SetDatumTargetType(XCAFDimTolObjects_DatumTargetType_Circle);
  target->SetDatumTargetLength(6.0);
  EXPECT_EQ(datumText(target), "A1");
  EXPECT_EQ(datumTargetTypeName(XCAFDimTolObjects_DatumTargetType_Circle), "Circle");
  EXPECT_EQ(datumTargetTypeName(XCAFDimTolObjects_DatumTargetType_Point), "Point");
  EXPECT_EQ(datumTargetTypeName(XCAFDimTolObjects_DatumTargetType_Line), "Line");
  EXPECT_EQ(datumTargetTypeName(XCAFDimTolObjects_DatumTargetType_Rectangle), "Rectangle");
  EXPECT_EQ(datumTargetTypeName(XCAFDimTolObjects_DatumTargetType_Area), "Area");

  const auto rows = datumProperties(target);
  EXPECT_TRUE(hasRow(rows, "Label", "A1"));
  EXPECT_TRUE(hasRow(rows, "Kind", "Datum target"));
  EXPECT_TRUE(hasRow(rows, "Target type", "Circle"));
  EXPECT_TRUE(hasRow(rows, "Target number", "1"));
  EXPECT_TRUE(hasRow(rows, "Target length", "6 mm"));
  EXPECT_FALSE(hasKey(rows, "Target width"));
}

TEST(PmiText, DatumProperties) {
  Datum datum = makeDatum("B");
  datum->AddModifier(XCAFDimTolObjects_DatumSingleModif_MaximumMaterialRequirement);
  datum->SetModifierWithValue(XCAFDimTolObjects_DatumModifWithValue_Projected, 12.5);
  const auto rows = datumProperties(datum);
  EXPECT_TRUE(hasRow(rows, "Label", "B"));
  EXPECT_TRUE(hasRow(rows, "Kind", "Datum"));
  EXPECT_TRUE(hasRow(rows, "Modifiers", "Maximum material condition (Ⓜ)"));
  EXPECT_TRUE(hasRow(rows, "Modifier with value", "Projected: 12.5 mm"));
  EXPECT_FALSE(hasKey(rows, "Target type"));

  // The same modifiers appear in the control-frame cell.
  Tol tol = makeTolerance(XCAFDimTolObjects_GeomToleranceType_Parallelism, 0.1);
  EXPECT_EQ(toleranceText(tol, {datum}), "∥ | 0.1 | B Ⓜ Ⓟ 12.5");
}

// --- Formatting hygiene -------------------------------------------------------------

TEST(PmiText, NeverPrintsNegativeZero) {
  Dim dim = makeDimension(XCAFDimTolObjects_DimensionType_Size_Diameter, -0.0);
  dim->SetUpperTolValue(0.0);
  dim->SetLowerTolValue(-0.0);
  dim->SetNbOfDecimalPlaces(1, 2);
  EXPECT_EQ(dimensionText(dim).find("-0"), std::string::npos);
  EXPECT_EQ(dimensionText(dim).find("−0"), std::string::npos) << dimensionText(dim);
  EXPECT_EQ(dimensionText(dim), "⌀0.00 0.00/0.00");

  // A deviation that rounds to zero loses its sign instead of showing "−0.00".
  Dim tiny = makeDimension(XCAFDimTolObjects_DimensionType_Size_Diameter, 5.0);
  tiny->SetUpperTolValue(0.1);
  tiny->SetLowerTolValue(-0.0004);
  EXPECT_EQ(dimensionText(tiny), "⌀5.00 +0.10/0.00");

  Tol tol = makeTolerance(XCAFDimTolObjects_GeomToleranceType_Flatness, -0.0);
  EXPECT_EQ(toleranceText(tol, {}), "⏥ | 0");
  for (const Property& row : dimensionProperties(dim)) {
    EXPECT_EQ(row.second.find("-0.0"), std::string::npos) << row.first << " = " << row.second;
  }
}

TEST(PmiText, OutputIsIndependentOfProcessLocale) {
  const char* previous = std::setlocale(LC_ALL, nullptr);
  const std::string savedC = previous ? previous : "C";
  const std::locale savedCxx;

  bool switched = false;
  for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "fr_FR.utf8"}) {
    if (std::setlocale(LC_ALL, name) != nullptr) {
      try {
        std::locale::global(std::locale(name));
        switched = true;
        break;
      } catch (const std::exception&) {
        // setlocale knows it but the C++ runtime does not; try the next one.
      }
    }
  }
  if (!switched) {
    std::setlocale(LC_ALL, savedC.c_str());
    GTEST_SKIP() << "no German/French UTF-8 locale installed";
  }

  Dim dim = makeDimension(XCAFDimTolObjects_DimensionType_Size_Diameter, 12.7);
  dim->SetUpperTolValue(0.10);
  dim->SetLowerTolValue(-0.05);
  const PositionFrame frame = makePositionFrame();
  const std::string dimension = dimensionText(dim);
  const std::string frameText = toleranceText(frame.tol, frame.datums);
  const auto properties = dimensionProperties(dim);

  std::locale::global(savedCxx);
  std::setlocale(LC_ALL, savedC.c_str());

  EXPECT_EQ(dimension, "⌀12.70 +0.10/−0.05");
  EXPECT_EQ(frameText, "⌖ | ⌀0.1 Ⓜ | A | B Ⓜ | C");
  EXPECT_TRUE(hasRow(properties, "Nominal", "12.700 mm"));
}
