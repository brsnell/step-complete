#pragma once

#include "core/OccHandle.h"
#include "core/Units.h"

#include <TDocStd_Document.hxx>

#include <memory>
#include <optional>
#include <string>

class TCollection_ExtendedString;

namespace sc::core {

// An XCAF document plus the facts we learned while importing it.
//
// Owns the OCAF document and closes it on destruction: documents are registered
// with the (global) XCAF application, so they would otherwise never be freed.
// Geometry inside the document is always in millimeters.
class Document {
public:
  Document();
  ~Document();

  Document(const Document&) = delete;
  Document& operator=(const Document&) = delete;

  const OccHandle<TDocStd_Document>& ocaf() const { return m_doc; }

  // Path the document was imported from (UTF-8), empty for in-memory documents.
  const std::string& sourcePath() const { return m_sourcePath; }
  void setSourcePath(std::string path) { m_sourcePath = std::move(path); }

  // Length unit declared in the source file, e.g. "inch". Empty if unknown.
  const std::string& fileUnitName() const { return m_fileUnitName; }
  std::optional<LengthUnit> fileUnit() const { return m_fileUnit; }
  void setFileUnit(std::string name, std::optional<LengthUnit> unit);

private:
  OccHandle<TDocStd_Document> m_doc;
  std::string m_sourcePath;
  std::string m_fileUnitName;
  std::optional<LengthUnit> m_fileUnit;
};

using DocumentPtr = std::shared_ptr<Document>;

// UTF-8 conversion for OCCT's wide strings (names in STEP files are often non-ASCII).
std::string toUtf8(const TCollection_ExtendedString& text);

// Maps STEP SI/conversion-based unit names ("millimetre", "INCH", ...) to a unit.
std::optional<LengthUnit> lengthUnitFromStepName(const std::string& name);

} // namespace sc::core
