#include "core/PmiText.h"

#include "core/Units.h"

#include <TCollection_HAsciiString.hxx>
#include <gp_Dir.hxx>

#include <cctype>
#include <cmath>
#include <string>

namespace sc::core::pmi {

namespace {

// --- Symbols (UTF-8; MSVC builds use /utf-8) ------------------------------------------

constexpr const char* kDiameter = "⌀";
constexpr const char* kSphericalDiameter = "S⌀";
constexpr const char* kSquare = "◻";
constexpr const char* kDegree = "°";
constexpr const char* kMinus = "−";
constexpr const char* kPlusMinus = "±";
constexpr const char* kEnDash = "–";
constexpr const char* kMmc = "Ⓜ";
constexpr const char* kLmc = "Ⓛ";
constexpr const char* kFreeState = "Ⓕ";
constexpr const char* kProjected = "Ⓟ";
constexpr const char* kTangentPlane = "Ⓣ";
constexpr const char* kUnequallyDisposed = "Ⓤ";
constexpr const char* kReciprocity = "Ⓡ";
constexpr const char* kCellSeparator = " | ";

// --- Number helpers -------------------------------------------------------------------

bool isZeroText(const std::string& text) {
  for (char c : text) {
    if (c >= '1' && c <= '9') {
      return false;
    }
  }
  return true;
}

// Fixed decimals, never "-0.00".
std::string fixedNumber(double value, int decimals) {
  return formatNumber(value, decimals);
}

// Fixed decimals with trailing zeros removed ("0.100" -> "0.1", "25.000" -> "25"). Used for
// geometric tolerance values, which are written without padding on drawings.
std::string trimmedNumber(double value, int decimals) {
  std::string text = formatNumber(value, decimals);
  if (text.find('.') != std::string::npos) {
    while (!text.empty() && text.back() == '0') {
      text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
      text.pop_back();
    }
  }
  if (text.empty() || text == "-") {
    text = "0";
  }
  return text;
}

// "+0.10", "−0.05" (U+2212) or "0.00": the sign of a deviation as drawn.
std::string signedDeviation(double value, int decimals, const char* suffix) {
  std::string text = formatNumber(value, decimals);
  std::string result;
  if (!text.empty() && text.front() == '-') {
    result = std::string(kMinus) + text.substr(1);
  } else if (isZeroText(text)) {
    result = text;
  } else {
    result = "+" + text;
  }
  return result + suffix;
}

std::string toText(const OccHandle<TCollection_HAsciiString>& s) {
  if (s.IsNull()) {
    return {};
  }
  const char* c = s->ToCString();
  return c ? std::string(c) : std::string();
}

void appendWord(std::string& text, const std::string& word) {
  if (word.empty()) {
    return;
  }
  if (!text.empty()) {
    text += ' ';
  }
  text += word;
}

std::string join(const std::vector<std::string>& parts, const char* separator) {
  std::string out;
  for (const std::string& part : parts) {
    if (!out.empty()) {
      out += separator;
    }
    out += part;
  }
  return out;
}

// --- Dimension helpers ----------------------------------------------------------------

bool isAngular(XCAFDimTolObjects_DimensionType type) {
  return type == XCAFDimTolObjects_DimensionType_Location_Angular ||
         type == XCAFDimTolObjects_DimensionType_Size_Angular;
}

std::string dimensionPrefix(XCAFDimTolObjects_DimensionType type) {
  switch (type) {
  case XCAFDimTolObjects_DimensionType_Size_Diameter:
  case XCAFDimTolObjects_DimensionType_Size_ToroidalMinorDiameter:
  case XCAFDimTolObjects_DimensionType_Size_ToroidalMajorDiameter:
  case XCAFDimTolObjects_DimensionType_Size_ToroidalHighMajorDiameter:
  case XCAFDimTolObjects_DimensionType_Size_ToroidalLowMajorDiameter:
    return kDiameter;
  case XCAFDimTolObjects_DimensionType_Size_SphericalDiameter:
    return kSphericalDiameter;
  case XCAFDimTolObjects_DimensionType_Size_Radius:
  case XCAFDimTolObjects_DimensionType_Size_ToroidalMinorRadius:
  case XCAFDimTolObjects_DimensionType_Size_ToroidalMajorRadius:
  case XCAFDimTolObjects_DimensionType_Size_ToroidalHighMajorRadius:
  case XCAFDimTolObjects_DimensionType_Size_ToroidalLowMajorRadius:
    return "R";
  case XCAFDimTolObjects_DimensionType_Size_SphericalRadius:
    return "SR";
  default:
    return {};
  }
}

// Short drawing text: ISO 14405 size-specification codes where they exist.
std::string dimensionModifierSymbol(XCAFDimTolObjects_DimensionModif modifier) {
  switch (modifier) {
  case XCAFDimTolObjects_DimensionModif_ControlledRadius:
    return "CR";
  case XCAFDimTolObjects_DimensionModif_Square:
    return kSquare;
  case XCAFDimTolObjects_DimensionModif_StatisticalTolerance:
    return "(ST)";
  case XCAFDimTolObjects_DimensionModif_ContinuousFeature:
    return "CF";
  case XCAFDimTolObjects_DimensionModif_TwoPointSize:
    return "LP";
  case XCAFDimTolObjects_DimensionModif_LocalSizeDefinedBySphere:
    return "LS";
  case XCAFDimTolObjects_DimensionModif_LeastSquaresAssociationCriterion:
    return "GG";
  case XCAFDimTolObjects_DimensionModif_MaximumInscribedAssociation:
    return "GX";
  case XCAFDimTolObjects_DimensionModif_MinimumCircumscribedAssociation:
    return "GN";
  case XCAFDimTolObjects_DimensionModif_CircumferenceDiameter:
    return "CC";
  case XCAFDimTolObjects_DimensionModif_AreaDiameter:
    return "CA";
  case XCAFDimTolObjects_DimensionModif_VolumeDiameter:
    return "CV";
  case XCAFDimTolObjects_DimensionModif_MaximumSize:
    return "SX";
  case XCAFDimTolObjects_DimensionModif_MinimumSize:
    return "SN";
  case XCAFDimTolObjects_DimensionModif_AverageSize:
    return "SA";
  case XCAFDimTolObjects_DimensionModif_MedianSize:
    return "SM";
  case XCAFDimTolObjects_DimensionModif_MidRangeSize:
    return "SR";
  case XCAFDimTolObjects_DimensionModif_RangeOfSizes:
    return "SD";
  case XCAFDimTolObjects_DimensionModif_AnyRestrictedPortionOfFeature:
    return "(any portion)";
  case XCAFDimTolObjects_DimensionModif_AnyCrossSection:
    return "ACS";
  case XCAFDimTolObjects_DimensionModif_SpecificFixedCrossSection:
    return "SCS";
  case XCAFDimTolObjects_DimensionModif_CommonTolerance:
    return "(common tolerance)";
  case XCAFDimTolObjects_DimensionModif_FreeStateCondition:
    return kFreeState;
  case XCAFDimTolObjects_DimensionModif_Between:
    return "(between)";
  default:
    return "(modifier " + std::to_string(static_cast<int>(modifier)) + ")";
  }
}

std::string dimensionModifierName(XCAFDimTolObjects_DimensionModif modifier) {
  switch (modifier) {
  case XCAFDimTolObjects_DimensionModif_ControlledRadius:
    return "Controlled radius";
  case XCAFDimTolObjects_DimensionModif_Square:
    return "Square";
  case XCAFDimTolObjects_DimensionModif_StatisticalTolerance:
    return "Statistical tolerance";
  case XCAFDimTolObjects_DimensionModif_ContinuousFeature:
    return "Continuous feature";
  case XCAFDimTolObjects_DimensionModif_TwoPointSize:
    return "Two-point size";
  case XCAFDimTolObjects_DimensionModif_LocalSizeDefinedBySphere:
    return "Local size defined by a sphere";
  case XCAFDimTolObjects_DimensionModif_LeastSquaresAssociationCriterion:
    return "Least-squares association criterion";
  case XCAFDimTolObjects_DimensionModif_MaximumInscribedAssociation:
    return "Maximum inscribed association criterion";
  case XCAFDimTolObjects_DimensionModif_MinimumCircumscribedAssociation:
    return "Minimum circumscribed association criterion";
  case XCAFDimTolObjects_DimensionModif_CircumferenceDiameter:
    return "Circumference diameter";
  case XCAFDimTolObjects_DimensionModif_AreaDiameter:
    return "Area diameter";
  case XCAFDimTolObjects_DimensionModif_VolumeDiameter:
    return "Volume diameter";
  case XCAFDimTolObjects_DimensionModif_MaximumSize:
    return "Maximum size";
  case XCAFDimTolObjects_DimensionModif_MinimumSize:
    return "Minimum size";
  case XCAFDimTolObjects_DimensionModif_AverageSize:
    return "Average size";
  case XCAFDimTolObjects_DimensionModif_MedianSize:
    return "Median size";
  case XCAFDimTolObjects_DimensionModif_MidRangeSize:
    return "Mid-range size";
  case XCAFDimTolObjects_DimensionModif_RangeOfSizes:
    return "Range of sizes";
  case XCAFDimTolObjects_DimensionModif_AnyRestrictedPortionOfFeature:
    return "Any restricted portion of feature";
  case XCAFDimTolObjects_DimensionModif_AnyCrossSection:
    return "Any cross section";
  case XCAFDimTolObjects_DimensionModif_SpecificFixedCrossSection:
    return "Specific fixed cross section";
  case XCAFDimTolObjects_DimensionModif_CommonTolerance:
    return "Common tolerance";
  case XCAFDimTolObjects_DimensionModif_FreeStateCondition:
    return "Free state condition";
  case XCAFDimTolObjects_DimensionModif_Between:
    return "Between";
  default:
    return "Modifier " + std::to_string(static_cast<int>(modifier));
  }
}

std::string qualifierSuffix(XCAFDimTolObjects_DimensionQualifier qualifier) {
  switch (qualifier) {
  case XCAFDimTolObjects_DimensionQualifier_None:
    return {};
  case XCAFDimTolObjects_DimensionQualifier_Min:
    return "min";
  case XCAFDimTolObjects_DimensionQualifier_Max:
    return "max";
  case XCAFDimTolObjects_DimensionQualifier_Avg:
    return "avg";
  default:
    return "(qualifier " + std::to_string(static_cast<int>(qualifier)) + ")";
  }
}

std::string qualifierName(XCAFDimTolObjects_DimensionQualifier qualifier) {
  switch (qualifier) {
  case XCAFDimTolObjects_DimensionQualifier_None:
    return {};
  case XCAFDimTolObjects_DimensionQualifier_Min:
    return "Minimum";
  case XCAFDimTolObjects_DimensionQualifier_Max:
    return "Maximum";
  case XCAFDimTolObjects_DimensionQualifier_Avg:
    return "Average";
  default:
    return "Qualifier " + std::to_string(static_cast<int>(qualifier));
  }
}

// ISO 286 letter ("H", "js", "p"); upper case for holes, lower case for shafts.
std::string formVarianceLetters(XCAFDimTolObjects_DimensionFormVariance variance, bool hole) {
  const char* letters = nullptr;
  switch (variance) {
  case XCAFDimTolObjects_DimensionFormVariance_None:
    return {};
  case XCAFDimTolObjects_DimensionFormVariance_A:
    letters = "A";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_B:
    letters = "B";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_C:
    letters = "C";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_CD:
    letters = "CD";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_D:
    letters = "D";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_E:
    letters = "E";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_EF:
    letters = "EF";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_F:
    letters = "F";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_FG:
    letters = "FG";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_G:
    letters = "G";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_H:
    letters = "H";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_JS:
    letters = "JS";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_J:
    letters = "J";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_K:
    letters = "K";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_M:
    letters = "M";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_N:
    letters = "N";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_P:
    letters = "P";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_R:
    letters = "R";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_S:
    letters = "S";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_T:
    letters = "T";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_U:
    letters = "U";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_V:
    letters = "V";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_X:
    letters = "X";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_Y:
    letters = "Y";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_Z:
    letters = "Z";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_ZA:
    letters = "ZA";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_ZB:
    letters = "ZB";
    break;
  case XCAFDimTolObjects_DimensionFormVariance_ZC:
    letters = "ZC";
    break;
  default:
    return "?";
  }
  std::string text = letters;
  if (!hole) {
    for (char& c : text) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }
  return text;
}

// "IT7" -> "7", "IT01" -> "01".
std::string gradeNumber(XCAFDimTolObjects_DimensionGrade grade) {
  switch (grade) {
  case XCAFDimTolObjects_DimensionGrade_IT01:
    return "01";
  case XCAFDimTolObjects_DimensionGrade_IT0:
    return "0";
  case XCAFDimTolObjects_DimensionGrade_IT1:
    return "1";
  case XCAFDimTolObjects_DimensionGrade_IT2:
    return "2";
  case XCAFDimTolObjects_DimensionGrade_IT3:
    return "3";
  case XCAFDimTolObjects_DimensionGrade_IT4:
    return "4";
  case XCAFDimTolObjects_DimensionGrade_IT5:
    return "5";
  case XCAFDimTolObjects_DimensionGrade_IT6:
    return "6";
  case XCAFDimTolObjects_DimensionGrade_IT7:
    return "7";
  case XCAFDimTolObjects_DimensionGrade_IT8:
    return "8";
  case XCAFDimTolObjects_DimensionGrade_IT9:
    return "9";
  case XCAFDimTolObjects_DimensionGrade_IT10:
    return "10";
  case XCAFDimTolObjects_DimensionGrade_IT11:
    return "11";
  case XCAFDimTolObjects_DimensionGrade_IT12:
    return "12";
  case XCAFDimTolObjects_DimensionGrade_IT13:
    return "13";
  case XCAFDimTolObjects_DimensionGrade_IT14:
    return "14";
  case XCAFDimTolObjects_DimensionGrade_IT15:
    return "15";
  case XCAFDimTolObjects_DimensionGrade_IT16:
    return "16";
  case XCAFDimTolObjects_DimensionGrade_IT17:
    return "17";
  case XCAFDimTolObjects_DimensionGrade_IT18:
    return "18";
  default:
    return "?";
  }
}

// Everything needed to print one dimension; computed once and shared by text and properties.
struct DimensionView {
  XCAFDimTolObjects_DimensionType type = XCAFDimTolObjects_DimensionType_Location_None;
  bool angular = false;
  int decimals = 2;
  bool explicitDecimals = false;
  double unit = 1.0; // value multiplier (1: OCCT already stores angles in degrees, lengths in mm)
  const char* suffix = "";

  bool range = false;
  bool plusMinus = false;
  double nominal = 0.0;
  double lower = 0.0; // range: lower limit; plus/minus: lower deviation
  double upper = 0.0; // range: upper limit; plus/minus: upper deviation

  bool hasClass = false;
  bool hole = false;
  XCAFDimTolObjects_DimensionFormVariance variance = XCAFDimTolObjects_DimensionFormVariance_None;
  XCAFDimTolObjects_DimensionGrade grade = XCAFDimTolObjects_DimensionGrade_IT01;

  XCAFDimTolObjects_DimensionQualifier qualifier = XCAFDimTolObjects_DimensionQualifier_None;
  std::vector<XCAFDimTolObjects_DimensionModif> modifiers;
};

DimensionView viewOf(const XCAFDimTolObjects_DimensionObject& dim, int fallbackDecimals) {
  DimensionView v;
  v.type = dim.GetType();
  v.angular = isAngular(v.type);
  if (v.angular) {
    // OCCT's STEP reader converts angular dimension values to degrees
    // (STEPCAFControl_Reader: convertAngleValue).
    v.suffix = kDegree;
  }

  int left = 0;
  int right = 0;
  dim.GetNbOfDecimalPlaces(left, right);
  v.explicitDecimals = right > 0;
  v.decimals = right > 0 ? right : fallbackDecimals;

  if (dim.IsDimWithRange()) {
    v.range = true;
    v.lower = dim.GetLowerBound() * v.unit;
    v.upper = dim.GetUpperBound() * v.unit;
    v.nominal = dim.GetValue() * v.unit;
  } else if (dim.IsDimWithPlusMinusTolerance()) {
    v.plusMinus = true;
    v.nominal = dim.GetValue() * v.unit;
    // OCCT stores plus/minus dimensions as (nominal, |lower deviation|, upper
    // deviation): the reader keeps only the magnitude of the lower bound, which
    // always means "minus". Accept either sign for objects built in code.
    v.lower = -std::abs(dim.GetLowerTolValue()) * v.unit;
    v.upper = dim.GetUpperTolValue() * v.unit;
  } else {
    v.nominal = dim.GetValue() * v.unit;
  }

  if (dim.IsDimWithClassOfTolerance()) {
    bool hole = false;
    XCAFDimTolObjects_DimensionFormVariance variance = XCAFDimTolObjects_DimensionFormVariance_None;
    XCAFDimTolObjects_DimensionGrade grade = XCAFDimTolObjects_DimensionGrade_IT01;
    if (dim.GetClassOfTolerance(hole, variance, grade)) {
      v.hasClass = true;
      v.hole = hole;
      v.variance = variance;
      v.grade = grade;
    }
  }

  if (dim.HasQualifier()) {
    v.qualifier = dim.GetQualifier();
  }

  const auto modifiers = dim.GetModifiers();
  for (int i = 1; i <= modifiers.Length(); ++i) {
    v.modifiers.push_back(modifiers.Value(i));
  }
  return v;
}

std::string fitClassText(const DimensionView& v) {
  return formVarianceLetters(v.variance, v.hole) + gradeNumber(v.grade);
}

std::string dimensionBody(const DimensionView& v) {
  std::string prefix = dimensionPrefix(v.type);
  std::vector<std::string> suffixModifiers;
  for (XCAFDimTolObjects_DimensionModif modifier : v.modifiers) {
    if (modifier == XCAFDimTolObjects_DimensionModif_ControlledRadius) {
      prefix = prefix == "R" ? "CR" : "CR" + prefix;
    } else if (modifier == XCAFDimTolObjects_DimensionModif_Square) {
      prefix = kSquare + prefix;
    } else {
      suffixModifiers.push_back(dimensionModifierSymbol(modifier));
    }
  }

  const auto number = [&](double value) { return fixedNumber(value, v.decimals) + v.suffix; };

  std::string text;
  if (v.range) {
    text = prefix + number(v.lower) + kEnDash + number(v.upper);
  } else {
    // A fit class is conventionally written with an unpadded nominal ("⌀12 H7").
    const bool trim = v.hasClass && !v.explicitDecimals;
    text = prefix + (trim ? trimmedNumber(v.nominal, v.decimals) + v.suffix : number(v.nominal));
  }
  if (v.hasClass) {
    appendWord(text, fitClassText(v));
  }
  if (v.plusMinus) {
    const bool symmetric = std::abs(v.upper + v.lower) < 1e-9 && v.upper > 0.0;
    if (symmetric) {
      appendWord(text, std::string(kPlusMinus) + fixedNumber(v.upper, v.decimals) + v.suffix);
    } else {
      appendWord(text, signedDeviation(v.upper, v.decimals, v.suffix) + "/" +
                           signedDeviation(v.lower, v.decimals, v.suffix));
    }
  }
  appendWord(text, qualifierSuffix(v.qualifier));
  for (const std::string& modifier : suffixModifiers) {
    appendWord(text, modifier);
  }
  return text;
}

std::string lengthOrAngle(const DimensionView& v, double value) {
  if (v.angular) {
    return fixedNumber(value, v.decimals) + v.suffix;
  }
  return fixedNumber(value, v.decimals) + " mm";
}

// --- Tolerance helpers ----------------------------------------------------------------

std::string toleranceModifierSymbol(XCAFDimTolObjects_GeomToleranceModif modifier) {
  switch (modifier) {
  case XCAFDimTolObjects_GeomToleranceModif_Any_Cross_Section:
    return "ACS";
  case XCAFDimTolObjects_GeomToleranceModif_Common_Zone:
    return "CZ";
  case XCAFDimTolObjects_GeomToleranceModif_Each_Radial_Element:
    return "ERE";
  case XCAFDimTolObjects_GeomToleranceModif_Free_State:
    return kFreeState;
  case XCAFDimTolObjects_GeomToleranceModif_Least_Material_Requirement:
    return kLmc;
  case XCAFDimTolObjects_GeomToleranceModif_Line_Element:
    return "LE";
  case XCAFDimTolObjects_GeomToleranceModif_Major_Diameter:
    return "MD";
  case XCAFDimTolObjects_GeomToleranceModif_Maximum_Material_Requirement:
    return kMmc;
  case XCAFDimTolObjects_GeomToleranceModif_Minor_Diameter:
    return "LD";
  case XCAFDimTolObjects_GeomToleranceModif_Not_Convex:
    return "NC";
  case XCAFDimTolObjects_GeomToleranceModif_Pitch_Diameter:
    return "PD";
  case XCAFDimTolObjects_GeomToleranceModif_Reciprocity_Requirement:
    return kReciprocity;
  case XCAFDimTolObjects_GeomToleranceModif_Separate_Requirement:
    return "SEP";
  case XCAFDimTolObjects_GeomToleranceModif_Statistical_Tolerance:
    return "(ST)";
  case XCAFDimTolObjects_GeomToleranceModif_Tangent_Plane:
    return kTangentPlane;
  case XCAFDimTolObjects_GeomToleranceModif_All_Around:
    return "(all around)";
  case XCAFDimTolObjects_GeomToleranceModif_All_Over:
    return "(all over)";
  default:
    return "(modifier " + std::to_string(static_cast<int>(modifier)) + ")";
  }
}

std::string toleranceModifierName(XCAFDimTolObjects_GeomToleranceModif modifier) {
  switch (modifier) {
  case XCAFDimTolObjects_GeomToleranceModif_Any_Cross_Section:
    return "Any cross section";
  case XCAFDimTolObjects_GeomToleranceModif_Common_Zone:
    return "Common zone";
  case XCAFDimTolObjects_GeomToleranceModif_Each_Radial_Element:
    return "Each radial element";
  case XCAFDimTolObjects_GeomToleranceModif_Free_State:
    return "Free state";
  case XCAFDimTolObjects_GeomToleranceModif_Least_Material_Requirement:
    return "Least material condition";
  case XCAFDimTolObjects_GeomToleranceModif_Line_Element:
    return "Line element";
  case XCAFDimTolObjects_GeomToleranceModif_Major_Diameter:
    return "Major diameter";
  case XCAFDimTolObjects_GeomToleranceModif_Maximum_Material_Requirement:
    return "Maximum material condition";
  case XCAFDimTolObjects_GeomToleranceModif_Minor_Diameter:
    return "Minor diameter";
  case XCAFDimTolObjects_GeomToleranceModif_Not_Convex:
    return "Not convex";
  case XCAFDimTolObjects_GeomToleranceModif_Pitch_Diameter:
    return "Pitch diameter";
  case XCAFDimTolObjects_GeomToleranceModif_Reciprocity_Requirement:
    return "Reciprocity requirement";
  case XCAFDimTolObjects_GeomToleranceModif_Separate_Requirement:
    return "Separate requirement";
  case XCAFDimTolObjects_GeomToleranceModif_Statistical_Tolerance:
    return "Statistical tolerance";
  case XCAFDimTolObjects_GeomToleranceModif_Tangent_Plane:
    return "Tangent plane";
  case XCAFDimTolObjects_GeomToleranceModif_All_Around:
    return "All around";
  case XCAFDimTolObjects_GeomToleranceModif_All_Over:
    return "All over";
  default:
    return "Modifier " + std::to_string(static_cast<int>(modifier));
  }
}

std::string materialRequirementSymbol(XCAFDimTolObjects_GeomToleranceMatReqModif modifier) {
  switch (modifier) {
  case XCAFDimTolObjects_GeomToleranceMatReqModif_None:
    return {};
  case XCAFDimTolObjects_GeomToleranceMatReqModif_M:
    return kMmc;
  case XCAFDimTolObjects_GeomToleranceMatReqModif_L:
    return kLmc;
  default:
    return "(material " + std::to_string(static_cast<int>(modifier)) + ")";
  }
}

std::string materialRequirementName(XCAFDimTolObjects_GeomToleranceMatReqModif modifier) {
  switch (modifier) {
  case XCAFDimTolObjects_GeomToleranceMatReqModif_None:
    return {};
  case XCAFDimTolObjects_GeomToleranceMatReqModif_M:
    return "Maximum material condition (M)";
  case XCAFDimTolObjects_GeomToleranceMatReqModif_L:
    return "Least material condition (L)";
  default:
    return "Material requirement " + std::to_string(static_cast<int>(modifier));
  }
}

std::string zoneModifierSymbol(XCAFDimTolObjects_GeomToleranceZoneModif modifier) {
  switch (modifier) {
  case XCAFDimTolObjects_GeomToleranceZoneModif_None:
    return {};
  case XCAFDimTolObjects_GeomToleranceZoneModif_Projected:
    return kProjected;
  case XCAFDimTolObjects_GeomToleranceZoneModif_Runout:
    return "(runout zone)";
  case XCAFDimTolObjects_GeomToleranceZoneModif_NonUniform:
    return kUnequallyDisposed;
  default:
    return "(zone " + std::to_string(static_cast<int>(modifier)) + ")";
  }
}

std::string zoneModifierName(XCAFDimTolObjects_GeomToleranceZoneModif modifier) {
  switch (modifier) {
  case XCAFDimTolObjects_GeomToleranceZoneModif_None:
    return {};
  case XCAFDimTolObjects_GeomToleranceZoneModif_Projected:
    return "Projected tolerance zone (P)";
  case XCAFDimTolObjects_GeomToleranceZoneModif_Runout:
    return "Runout zone";
  case XCAFDimTolObjects_GeomToleranceZoneModif_NonUniform:
    return "Unequally disposed (U)";
  default:
    return "Zone modifier " + std::to_string(static_cast<int>(modifier));
  }
}

std::string zoneShapePrefix(XCAFDimTolObjects_GeomToleranceTypeValue shape) {
  switch (shape) {
  case XCAFDimTolObjects_GeomToleranceTypeValue_None:
    return {};
  case XCAFDimTolObjects_GeomToleranceTypeValue_Diameter:
    return kDiameter;
  case XCAFDimTolObjects_GeomToleranceTypeValue_SphericalDiameter:
    return kSphericalDiameter;
  default:
    return {};
  }
}

std::string zoneShapeName(XCAFDimTolObjects_GeomToleranceTypeValue shape) {
  switch (shape) {
  case XCAFDimTolObjects_GeomToleranceTypeValue_None:
    return {};
  case XCAFDimTolObjects_GeomToleranceTypeValue_Diameter:
    return "Cylindrical (diameter)";
  case XCAFDimTolObjects_GeomToleranceTypeValue_SphericalDiameter:
    return "Spherical (spherical diameter)";
  default:
    return "Zone shape " + std::to_string(static_cast<int>(shape));
  }
}

// The tolerance zone cell: "⌀0.1 Ⓜ", "0.05 Ⓟ 10", "0.1/25", "⌀0.2 Ⓜ Ⓕ".
std::string zoneCell(const XCAFDimTolObjects_GeomToleranceObject& tol, int decimals) {
  std::string text = zoneShapePrefix(tol.GetTypeOfValue()) + trimmedNumber(tol.GetValue(), decimals);
  const double perUnit = tol.GetMaxValueModifier();
  if (perUnit > 0.0) {
    text += "/" + trimmedNumber(perUnit, decimals);
  }

  std::vector<std::string> flags;
  const auto addFlag = [&flags](const std::string& flag) {
    if (flag.empty()) {
      return;
    }
    for (const std::string& existing : flags) {
      if (existing == flag) {
        return;
      }
    }
    flags.push_back(flag);
  };

  addFlag(materialRequirementSymbol(tol.GetMaterialRequirementModifier()));

  const XCAFDimTolObjects_GeomToleranceZoneModif zone = tol.GetZoneModifier();
  std::string zoneText = zoneModifierSymbol(zone);
  if (!zoneText.empty() && zone != XCAFDimTolObjects_GeomToleranceZoneModif_None) {
    const double zoneValue = tol.GetValueOfZoneModifier();
    if (zoneValue > 0.0) {
      zoneText += " " + trimmedNumber(zoneValue, decimals);
    }
    addFlag(zoneText);
  }

  const auto modifiers = tol.GetModifiers();
  for (int i = 1; i <= modifiers.Length(); ++i) {
    addFlag(toleranceModifierSymbol(modifiers.Value(i)));
  }

  for (const std::string& flag : flags) {
    appendWord(text, flag);
  }
  return text;
}

// --- Datum helpers --------------------------------------------------------------------

std::string datumSingleModifierSymbol(XCAFDimTolObjects_DatumSingleModif modifier) {
  switch (modifier) {
  case XCAFDimTolObjects_DatumSingleModif_AnyCrossSection:
    return "[ACS]";
  case XCAFDimTolObjects_DatumSingleModif_Any_LongitudinalSection:
    return "[ALS]";
  case XCAFDimTolObjects_DatumSingleModif_Basic:
    return "(basic)";
  case XCAFDimTolObjects_DatumSingleModif_ContactingFeature:
    return "[CF]";
  case XCAFDimTolObjects_DatumSingleModif_DegreeOfFreedomConstraintU:
    return "[U]";
  case XCAFDimTolObjects_DatumSingleModif_DegreeOfFreedomConstraintV:
    return "[V]";
  case XCAFDimTolObjects_DatumSingleModif_DegreeOfFreedomConstraintW:
    return "[W]";
  case XCAFDimTolObjects_DatumSingleModif_DegreeOfFreedomConstraintX:
    return "[X]";
  case XCAFDimTolObjects_DatumSingleModif_DegreeOfFreedomConstraintY:
    return "[Y]";
  case XCAFDimTolObjects_DatumSingleModif_DegreeOfFreedomConstraintZ:
    return "[Z]";
  case XCAFDimTolObjects_DatumSingleModif_DistanceVariable:
    return "[DV]";
  case XCAFDimTolObjects_DatumSingleModif_FreeState:
    return kFreeState;
  case XCAFDimTolObjects_DatumSingleModif_LeastMaterialRequirement:
    return kLmc;
  case XCAFDimTolObjects_DatumSingleModif_Line:
    return "[SL]";
  case XCAFDimTolObjects_DatumSingleModif_MajorDiameter:
    return "[MD]";
  case XCAFDimTolObjects_DatumSingleModif_MaximumMaterialRequirement:
    return kMmc;
  case XCAFDimTolObjects_DatumSingleModif_MinorDiameter:
    return "[LD]";
  case XCAFDimTolObjects_DatumSingleModif_Orientation:
    return "[O]";
  case XCAFDimTolObjects_DatumSingleModif_PitchDiameter:
    return "[PD]";
  case XCAFDimTolObjects_DatumSingleModif_Plane:
    return "[PL]";
  case XCAFDimTolObjects_DatumSingleModif_Point:
    return "[PT]";
  case XCAFDimTolObjects_DatumSingleModif_Translation:
    return "[T]";
  default:
    return "(modifier " + std::to_string(static_cast<int>(modifier)) + ")";
  }
}

std::string datumSingleModifierName(XCAFDimTolObjects_DatumSingleModif modifier) {
  switch (modifier) {
  case XCAFDimTolObjects_DatumSingleModif_AnyCrossSection:
    return "Any cross section";
  case XCAFDimTolObjects_DatumSingleModif_Any_LongitudinalSection:
    return "Any longitudinal section";
  case XCAFDimTolObjects_DatumSingleModif_Basic:
    return "Basic";
  case XCAFDimTolObjects_DatumSingleModif_ContactingFeature:
    return "Contacting feature";
  case XCAFDimTolObjects_DatumSingleModif_DegreeOfFreedomConstraintU:
    return "Degree of freedom constraint U";
  case XCAFDimTolObjects_DatumSingleModif_DegreeOfFreedomConstraintV:
    return "Degree of freedom constraint V";
  case XCAFDimTolObjects_DatumSingleModif_DegreeOfFreedomConstraintW:
    return "Degree of freedom constraint W";
  case XCAFDimTolObjects_DatumSingleModif_DegreeOfFreedomConstraintX:
    return "Degree of freedom constraint X";
  case XCAFDimTolObjects_DatumSingleModif_DegreeOfFreedomConstraintY:
    return "Degree of freedom constraint Y";
  case XCAFDimTolObjects_DatumSingleModif_DegreeOfFreedomConstraintZ:
    return "Degree of freedom constraint Z";
  case XCAFDimTolObjects_DatumSingleModif_DistanceVariable:
    return "Distance variable";
  case XCAFDimTolObjects_DatumSingleModif_FreeState:
    return "Free state";
  case XCAFDimTolObjects_DatumSingleModif_LeastMaterialRequirement:
    return "Least material condition";
  case XCAFDimTolObjects_DatumSingleModif_Line:
    return "Line";
  case XCAFDimTolObjects_DatumSingleModif_MajorDiameter:
    return "Major diameter";
  case XCAFDimTolObjects_DatumSingleModif_MaximumMaterialRequirement:
    return "Maximum material condition";
  case XCAFDimTolObjects_DatumSingleModif_MinorDiameter:
    return "Minor diameter";
  case XCAFDimTolObjects_DatumSingleModif_Orientation:
    return "Orientation";
  case XCAFDimTolObjects_DatumSingleModif_PitchDiameter:
    return "Pitch diameter";
  case XCAFDimTolObjects_DatumSingleModif_Plane:
    return "Plane";
  case XCAFDimTolObjects_DatumSingleModif_Point:
    return "Point";
  case XCAFDimTolObjects_DatumSingleModif_Translation:
    return "Translation";
  default:
    return "Modifier " + std::to_string(static_cast<int>(modifier));
  }
}

// "⌀2" / "S⌀2" / "Ⓟ 5" / "(distance 5)" for the single value-carrying datum modifier, or "".
std::string datumValueModifierText(const XCAFDimTolObjects_DatumObject& datum, int decimals, bool longForm,
                                   bool* present = nullptr) {
  XCAFDimTolObjects_DatumModifWithValue modifier = XCAFDimTolObjects_DatumModifWithValue_None;
  double value = 0.0;
  datum.GetModifierWithValue(modifier, value);
  if (present) {
    *present = modifier != XCAFDimTolObjects_DatumModifWithValue_None;
  }
  const std::string number = trimmedNumber(value, decimals);
  switch (modifier) {
  case XCAFDimTolObjects_DatumModifWithValue_None:
    return {};
  case XCAFDimTolObjects_DatumModifWithValue_CircularOrCylindrical:
    return longForm ? "Circular or cylindrical: " + number + " mm" : kDiameter + number;
  case XCAFDimTolObjects_DatumModifWithValue_Distance:
    return longForm ? "Distance: " + number + " mm" : "(distance " + number + ")";
  case XCAFDimTolObjects_DatumModifWithValue_Projected:
    return longForm ? "Projected: " + number + " mm" : std::string(kProjected) + " " + number;
  case XCAFDimTolObjects_DatumModifWithValue_Spherical:
    return longForm ? "Spherical: " + number + " mm" : kSphericalDiameter + number;
  default:
    return "(modifier " + std::to_string(static_cast<int>(modifier)) + ")";
  }
}

std::string datumCell(const OccHandle<XCAFDimTolObjects_DatumObject>& datum, int decimals) {
  std::string text = datumText(datum);
  const auto modifiers = datum->GetModifiers();
  for (int i = 1; i <= modifiers.Length(); ++i) {
    appendWord(text, datumSingleModifierSymbol(modifiers.Value(i)));
  }
  appendWord(text, datumValueModifierText(*datum, decimals, false));
  return text;
}

} // namespace

// --- Dimensions -----------------------------------------------------------------------

std::string dimensionTypeName(XCAFDimTolObjects_DimensionType type) {
  switch (type) {
  case XCAFDimTolObjects_DimensionType_Location_None:
    return "Location";
  case XCAFDimTolObjects_DimensionType_Location_CurvedDistance:
    return "Curved distance";
  case XCAFDimTolObjects_DimensionType_Location_LinearDistance:
    return "Linear distance";
  case XCAFDimTolObjects_DimensionType_Location_LinearDistance_FromCenterToOuter:
    return "Linear distance (center to outer)";
  case XCAFDimTolObjects_DimensionType_Location_LinearDistance_FromCenterToInner:
    return "Linear distance (center to inner)";
  case XCAFDimTolObjects_DimensionType_Location_LinearDistance_FromOuterToCenter:
    return "Linear distance (outer to center)";
  case XCAFDimTolObjects_DimensionType_Location_LinearDistance_FromOuterToOuter:
    return "Linear distance (outer to outer)";
  case XCAFDimTolObjects_DimensionType_Location_LinearDistance_FromOuterToInner:
    return "Linear distance (outer to inner)";
  case XCAFDimTolObjects_DimensionType_Location_LinearDistance_FromInnerToCenter:
    return "Linear distance (inner to center)";
  case XCAFDimTolObjects_DimensionType_Location_LinearDistance_FromInnerToOuter:
    return "Linear distance (inner to outer)";
  case XCAFDimTolObjects_DimensionType_Location_LinearDistance_FromInnerToInner:
    return "Linear distance (inner to inner)";
  case XCAFDimTolObjects_DimensionType_Location_Angular:
    return "Angular";
  case XCAFDimTolObjects_DimensionType_Location_Oriented:
    return "Oriented distance";
  case XCAFDimTolObjects_DimensionType_Location_WithPath:
    return "Distance along path";
  case XCAFDimTolObjects_DimensionType_Size_CurveLength:
    return "Curve length";
  case XCAFDimTolObjects_DimensionType_Size_Diameter:
    return "Diameter";
  case XCAFDimTolObjects_DimensionType_Size_SphericalDiameter:
    return "Spherical diameter";
  case XCAFDimTolObjects_DimensionType_Size_Radius:
    return "Radius";
  case XCAFDimTolObjects_DimensionType_Size_SphericalRadius:
    return "Spherical radius";
  case XCAFDimTolObjects_DimensionType_Size_ToroidalMinorDiameter:
    return "Toroidal minor diameter";
  case XCAFDimTolObjects_DimensionType_Size_ToroidalMajorDiameter:
    return "Toroidal major diameter";
  case XCAFDimTolObjects_DimensionType_Size_ToroidalMinorRadius:
    return "Toroidal minor radius";
  case XCAFDimTolObjects_DimensionType_Size_ToroidalMajorRadius:
    return "Toroidal major radius";
  case XCAFDimTolObjects_DimensionType_Size_ToroidalHighMajorDiameter:
    return "Toroidal high major diameter";
  case XCAFDimTolObjects_DimensionType_Size_ToroidalLowMajorDiameter:
    return "Toroidal low major diameter";
  case XCAFDimTolObjects_DimensionType_Size_ToroidalHighMajorRadius:
    return "Toroidal high major radius";
  case XCAFDimTolObjects_DimensionType_Size_ToroidalLowMajorRadius:
    return "Toroidal low major radius";
  case XCAFDimTolObjects_DimensionType_Size_Thickness:
    return "Thickness";
  case XCAFDimTolObjects_DimensionType_Size_Angular:
    return "Angular size";
  case XCAFDimTolObjects_DimensionType_Size_WithPath:
    return "Size along path";
  case XCAFDimTolObjects_DimensionType_CommonLabel:
    return "Common label";
  case XCAFDimTolObjects_DimensionType_DimensionPresentation:
    return "Dimension presentation";
  default:
    return "Dimension (type " + std::to_string(static_cast<int>(type)) + ")";
  }
}

std::string dimensionText(const OccHandle<XCAFDimTolObjects_DimensionObject>& dimension, int decimals) {
  if (dimension.IsNull()) {
    return {};
  }
  return dimensionBody(viewOf(*dimension, decimals));
}

std::vector<Property> dimensionProperties(const OccHandle<XCAFDimTolObjects_DimensionObject>& dimension,
                                          int decimals) {
  std::vector<Property> rows;
  if (dimension.IsNull()) {
    return rows;
  }
  const DimensionView v = viewOf(*dimension, decimals);

  rows.emplace_back("Type", dimensionTypeName(v.type));
  rows.emplace_back("Text", dimensionBody(v));
  const std::string semantic = toText(dimension->GetSemanticName());
  if (!semantic.empty()) {
    rows.emplace_back("Semantic name", semantic);
  }

  if (v.range) {
    rows.emplace_back("Tolerance kind", "Limits");
    rows.emplace_back("Lower limit", lengthOrAngle(v, v.lower));
    rows.emplace_back("Upper limit", lengthOrAngle(v, v.upper));
  } else {
    rows.emplace_back("Nominal", lengthOrAngle(v, v.nominal));
    if (v.plusMinus) {
      const bool symmetric = std::abs(v.upper + v.lower) < 1e-9 && v.upper > 0.0;
      rows.emplace_back("Tolerance kind", symmetric ? "Symmetric (\xC2\xB1)" : "Asymmetric (+/\xE2\x88\x92)");
      rows.emplace_back("Upper deviation",
                        signedDeviation(v.upper, v.decimals, v.angular ? v.suffix : " mm"));
      rows.emplace_back("Lower deviation",
                        signedDeviation(v.lower, v.decimals, v.angular ? v.suffix : " mm"));
    }
  }
  if (v.hasClass) {
    if (!v.range && !v.plusMinus) {
      rows.emplace_back("Tolerance kind", "Fit class");
    }
    rows.emplace_back("Fit class", fitClassText(v));
    rows.emplace_back("Applies to", v.hole ? "Hole (internal feature)" : "Shaft (external feature)");
  }
  if (!v.range && !v.plusMinus && !v.hasClass) {
    rows.emplace_back("Tolerance kind", "None");
  }

  if (v.qualifier != XCAFDimTolObjects_DimensionQualifier_None) {
    rows.emplace_back("Qualifier", qualifierName(v.qualifier));
  }
  if (!v.modifiers.empty()) {
    std::vector<std::string> names;
    for (XCAFDimTolObjects_DimensionModif modifier : v.modifiers) {
      names.push_back(dimensionModifierName(modifier) + " (" + dimensionModifierSymbol(modifier) + ")");
    }
    rows.emplace_back("Modifiers", join(names, ", "));
  }

  if (v.explicitDecimals) {
    int left = 0;
    int right = 0;
    dimension->GetNbOfDecimalPlaces(left, right);
    rows.emplace_back("Decimal places", std::to_string(left) + "." + std::to_string(right));
  }

  gp_Dir direction;
  if (dimension->GetDirection(direction)) {
    rows.emplace_back("Direction", "(" + trimmedNumber(direction.X(), 4) + ", " +
                                       trimmedNumber(direction.Y(), 4) + ", " +
                                       trimmedNumber(direction.Z(), 4) + ")");
  }

  for (int i = 1; i <= dimension->NbDescriptions(); ++i) {
    const std::string value = toText(dimension->GetDescription(i));
    std::string name = toText(dimension->GetDescriptionName(i));
    if (value.empty() && name.empty()) {
      continue;
    }
    rows.emplace_back(name.empty() ? "Description" : name, value);
  }

  const std::string presentation = toText(dimension->GetPresentationName());
  if (!presentation.empty()) {
    rows.emplace_back("Presentation", presentation);
  }
  return rows;
}

// --- Geometric tolerances -------------------------------------------------------------

std::string toleranceSymbol(XCAFDimTolObjects_GeomToleranceType type) {
  switch (type) {
  case XCAFDimTolObjects_GeomToleranceType_None:
    return {};
  case XCAFDimTolObjects_GeomToleranceType_Angularity:
    return "∠";
  case XCAFDimTolObjects_GeomToleranceType_CircularRunout:
    return "↗";
  case XCAFDimTolObjects_GeomToleranceType_CircularityOrRoundness:
    return "○";
  case XCAFDimTolObjects_GeomToleranceType_Coaxiality:
  case XCAFDimTolObjects_GeomToleranceType_Concentricity:
    return "◎";
  case XCAFDimTolObjects_GeomToleranceType_Cylindricity:
    return "⌭";
  case XCAFDimTolObjects_GeomToleranceType_Flatness:
    return "⏥";
  case XCAFDimTolObjects_GeomToleranceType_Parallelism:
    return "∥";
  case XCAFDimTolObjects_GeomToleranceType_Perpendicularity:
    return "⟂";
  case XCAFDimTolObjects_GeomToleranceType_Position:
    return "⌖";
  case XCAFDimTolObjects_GeomToleranceType_ProfileOfLine:
    return "⌒";
  case XCAFDimTolObjects_GeomToleranceType_ProfileOfSurface:
    return "⌓";
  case XCAFDimTolObjects_GeomToleranceType_Straightness:
    return "⏤";
  case XCAFDimTolObjects_GeomToleranceType_Symmetry:
    return "⌯";
  case XCAFDimTolObjects_GeomToleranceType_TotalRunout:
    return "⌰";
  default:
    return "?";
  }
}

std::string toleranceTypeName(XCAFDimTolObjects_GeomToleranceType type) {
  switch (type) {
  case XCAFDimTolObjects_GeomToleranceType_None:
    return "None";
  case XCAFDimTolObjects_GeomToleranceType_Angularity:
    return "Angularity";
  case XCAFDimTolObjects_GeomToleranceType_CircularRunout:
    return "Circular runout";
  case XCAFDimTolObjects_GeomToleranceType_CircularityOrRoundness:
    return "Circularity";
  case XCAFDimTolObjects_GeomToleranceType_Coaxiality:
    return "Coaxiality";
  case XCAFDimTolObjects_GeomToleranceType_Concentricity:
    return "Concentricity";
  case XCAFDimTolObjects_GeomToleranceType_Cylindricity:
    return "Cylindricity";
  case XCAFDimTolObjects_GeomToleranceType_Flatness:
    return "Flatness";
  case XCAFDimTolObjects_GeomToleranceType_Parallelism:
    return "Parallelism";
  case XCAFDimTolObjects_GeomToleranceType_Perpendicularity:
    return "Perpendicularity";
  case XCAFDimTolObjects_GeomToleranceType_Position:
    return "Position";
  case XCAFDimTolObjects_GeomToleranceType_ProfileOfLine:
    return "Profile of a line";
  case XCAFDimTolObjects_GeomToleranceType_ProfileOfSurface:
    return "Profile of a surface";
  case XCAFDimTolObjects_GeomToleranceType_Straightness:
    return "Straightness";
  case XCAFDimTolObjects_GeomToleranceType_Symmetry:
    return "Symmetry";
  case XCAFDimTolObjects_GeomToleranceType_TotalRunout:
    return "Total runout";
  default:
    return "Geometric tolerance (type " + std::to_string(static_cast<int>(type)) + ")";
  }
}

std::vector<std::string>
featureControlFrame(const OccHandle<XCAFDimTolObjects_GeomToleranceObject>& tolerance,
                    const std::vector<OccHandle<XCAFDimTolObjects_DatumObject>>& datums, int decimals) {
  std::vector<std::string> cells;
  if (tolerance.IsNull()) {
    return cells;
  }
  const std::string symbol = toleranceSymbol(tolerance->GetType());
  if (!symbol.empty()) {
    cells.push_back(symbol);
  }
  cells.push_back(zoneCell(*tolerance, decimals));
  for (const OccHandle<XCAFDimTolObjects_DatumObject>& datum : datums) {
    if (!datum.IsNull()) {
      cells.push_back(datumCell(datum, decimals));
    }
  }
  return cells;
}

std::string toleranceText(const OccHandle<XCAFDimTolObjects_GeomToleranceObject>& tolerance,
                          const std::vector<OccHandle<XCAFDimTolObjects_DatumObject>>& datums, int decimals) {
  return join(featureControlFrame(tolerance, datums, decimals), kCellSeparator);
}

std::vector<Property> toleranceProperties(const OccHandle<XCAFDimTolObjects_GeomToleranceObject>& tolerance,
                                          const std::vector<OccHandle<XCAFDimTolObjects_DatumObject>>& datums,
                                          int decimals) {
  std::vector<Property> rows;
  if (tolerance.IsNull()) {
    return rows;
  }
  const XCAFDimTolObjects_GeomToleranceType type = tolerance->GetType();
  rows.emplace_back("Type", toleranceTypeName(type));
  const std::string symbol = toleranceSymbol(type);
  if (!symbol.empty()) {
    rows.emplace_back("Symbol", symbol);
  }
  rows.emplace_back("Text", toleranceText(tolerance, datums, decimals));
  const std::string semantic = toText(tolerance->GetSemanticName());
  if (!semantic.empty()) {
    rows.emplace_back("Semantic name", semantic);
  }

  rows.emplace_back("Tolerance value", trimmedNumber(tolerance->GetValue(), decimals) + " mm");
  const std::string shape = zoneShapeName(tolerance->GetTypeOfValue());
  if (!shape.empty()) {
    rows.emplace_back("Zone shape", shape);
  }
  const std::string material = materialRequirementName(tolerance->GetMaterialRequirementModifier());
  if (!material.empty()) {
    rows.emplace_back("Material requirement", material);
  }
  const XCAFDimTolObjects_GeomToleranceZoneModif zone = tolerance->GetZoneModifier();
  if (zone != XCAFDimTolObjects_GeomToleranceZoneModif_None) {
    std::string text = zoneModifierName(zone);
    const double zoneValue = tolerance->GetValueOfZoneModifier();
    if (zoneValue > 0.0) {
      text += ": " + trimmedNumber(zoneValue, decimals) + " mm";
    }
    rows.emplace_back("Zone modifier", text);
  }
  const double perUnit = tolerance->GetMaxValueModifier();
  if (perUnit > 0.0) {
    rows.emplace_back("Per unit length", trimmedNumber(perUnit, decimals) + " mm");
  }

  const auto modifiers = tolerance->GetModifiers();
  if (modifiers.Length() > 0) {
    std::vector<std::string> names;
    for (int i = 1; i <= modifiers.Length(); ++i) {
      names.push_back(toleranceModifierName(modifiers.Value(i)) + " (" +
                      toleranceModifierSymbol(modifiers.Value(i)) + ")");
    }
    rows.emplace_back("Modifiers", join(names, ", "));
  }

  static const char* const kOrdinals[] = {"primary", "secondary", "tertiary"};
  int index = 0;
  for (const OccHandle<XCAFDimTolObjects_DatumObject>& datum : datums) {
    if (datum.IsNull()) {
      continue;
    }
    std::string label = "Datum " + std::to_string(index + 1);
    if (index < 3) {
      label += std::string(" (") + kOrdinals[index] + ")";
    }
    rows.emplace_back(label, datumCell(datum, decimals));
    ++index;
  }

  const std::string presentation = toText(tolerance->GetPresentationName());
  if (!presentation.empty()) {
    rows.emplace_back("Presentation", presentation);
  }
  return rows;
}

// --- Datums ---------------------------------------------------------------------------

std::string datumText(const OccHandle<XCAFDimTolObjects_DatumObject>& datum) {
  if (datum.IsNull()) {
    return {};
  }
  std::string text = toText(datum->GetName());
  if (datum->IsDatumTarget()) {
    const int number = datum->GetDatumTargetNumber();
    if (number > 0) {
      text += std::to_string(number);
    }
  }
  return text;
}

std::string datumTargetTypeName(XCAFDimTolObjects_DatumTargetType type) {
  switch (type) {
  case XCAFDimTolObjects_DatumTargetType_Point:
    return "Point";
  case XCAFDimTolObjects_DatumTargetType_Line:
    return "Line";
  case XCAFDimTolObjects_DatumTargetType_Rectangle:
    return "Rectangle";
  case XCAFDimTolObjects_DatumTargetType_Circle:
    return "Circle";
  case XCAFDimTolObjects_DatumTargetType_Area:
    return "Area";
  default:
    return "Target (type " + std::to_string(static_cast<int>(type)) + ")";
  }
}

std::vector<Property> datumProperties(const OccHandle<XCAFDimTolObjects_DatumObject>& datum, int decimals) {
  std::vector<Property> rows;
  if (datum.IsNull()) {
    return rows;
  }
  const bool target = datum->IsDatumTarget();
  rows.emplace_back("Label", datumText(datum));
  rows.emplace_back("Kind", target ? "Datum target" : "Datum");
  const std::string semantic = toText(datum->GetSemanticName());
  if (!semantic.empty()) {
    rows.emplace_back("Semantic name", semantic);
  }

  if (target) {
    rows.emplace_back("Target type", datumTargetTypeName(datum->GetDatumTargetType()));
    if (datum->GetDatumTargetNumber() > 0) {
      rows.emplace_back("Target number", std::to_string(datum->GetDatumTargetNumber()));
    }
    const double length = datum->GetDatumTargetLength();
    const double width = datum->GetDatumTargetWidth();
    if (length > 0.0) {
      rows.emplace_back("Target length", trimmedNumber(length, decimals) + " mm");
    }
    if (width > 0.0) {
      rows.emplace_back("Target width", trimmedNumber(width, decimals) + " mm");
    }
  }

  const auto modifiers = datum->GetModifiers();
  if (modifiers.Length() > 0) {
    std::vector<std::string> names;
    for (int i = 1; i <= modifiers.Length(); ++i) {
      names.push_back(datumSingleModifierName(modifiers.Value(i)) + " (" +
                      datumSingleModifierSymbol(modifiers.Value(i)) + ")");
    }
    rows.emplace_back("Modifiers", join(names, ", "));
  }
  bool hasValueModifier = false;
  const std::string valueModifier = datumValueModifierText(*datum, decimals, true, &hasValueModifier);
  if (hasValueModifier) {
    rows.emplace_back("Modifier with value", valueModifier);
  }

  if (datum->GetPosition() > 0) {
    rows.emplace_back("Position in frame", std::to_string(datum->GetPosition()));
  }
  const std::string presentation = toText(datum->GetPresentationName());
  if (!presentation.empty()) {
    rows.emplace_back("Presentation", presentation);
  }
  return rows;
}

} // namespace sc::core::pmi
