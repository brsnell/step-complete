#include "core/Document.h"

#include <TCollection_AsciiString.hxx>
#include <TCollection_ExtendedString.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>

#include <algorithm>
#include <cctype>

namespace sc::core {

Document::Document() {
  OccHandle<XCAFApp_Application> app = XCAFApp_Application::GetApplication();
  app->NewDocument(TCollection_ExtendedString("BinXCAF"), m_doc);
  // All geometry is stored in millimeters; the STEP reader converts into this unit.
  XCAFDoc_DocumentTool::SetLengthUnit(m_doc, 0.001);
}

Document::~Document() {
  if (!m_doc.IsNull() && m_doc->IsOpened()) {
    XCAFApp_Application::GetApplication()->Close(m_doc);
  }
}

void Document::setFileUnit(std::string name, std::optional<LengthUnit> unit) {
  m_fileUnitName = std::move(name);
  m_fileUnit = unit;
}

std::string toUtf8(const TCollection_ExtendedString& text) {
  // A replacement character of 0 makes OCCT convert to UTF-8 instead of ASCII.
  const TCollection_AsciiString utf8(text, '\0');
  return std::string(utf8.ToCString(), static_cast<size_t>(utf8.Length()));
}

std::optional<LengthUnit> lengthUnitFromStepName(const std::string& name) {
  std::string key;
  for (const char c : name) {
    if (std::isalpha(static_cast<unsigned char>(c))) {
      key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }
  if (key == "millimetre" || key == "millimeter" || key == "mm") {
    return LengthUnit::Millimeter;
  }
  if (key == "centimetre" || key == "centimeter" || key == "cm") {
    return LengthUnit::Centimeter;
  }
  if (key == "metre" || key == "meter" || key == "m") {
    return LengthUnit::Meter;
  }
  if (key == "micrometre" || key == "micrometer" || key == "micron") {
    return LengthUnit::Micrometer;
  }
  if (key == "inch" || key == "in") {
    return LengthUnit::Inch;
  }
  if (key == "foot" || key == "feet" || key == "ft") {
    return LengthUnit::Foot;
  }
  return std::nullopt;
}

} // namespace sc::core
