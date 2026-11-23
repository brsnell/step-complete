#pragma once

#include "core/FileFormat.h"
#include "core/OccHandle.h"
#include "core/StepCamera.h"
#include "core/Units.h"

#include <TDocStd_Document.hxx>

#include <memory>
#include <optional>
#include <string>
#include <vector>

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
  // Adopts an opened document that is registered with the XCAF application (e.g. loaded from disk).
  explicit Document(OccHandle<TDocStd_Document> adopted);
  ~Document();

  Document(const Document&) = delete;
  Document& operator=(const Document&) = delete;

  const OccHandle<TDocStd_Document>& ocaf() const { return _doc; }

  // Path the document was imported from (UTF-8), empty for in-memory documents.
  const std::string& sourcePath() const { return _sourcePath; }
  void setSourcePath(std::string path) { _sourcePath = std::move(path); }

  // The format the document was read from.
  FileFormat format() const { return _format; }
  void setFormat(FileFormat format) { _format = format; }

  // Length unit declared in the source file, e.g. "inch". Empty if unknown.
  const std::string& fileUnitName() const { return _fileUnitName; }
  std::optional<LengthUnit> fileUnit() const { return _fileUnit; }
  void setFileUnit(std::string name, std::optional<LengthUnit> unit);

  // Saved-view cameras read from the STEP file, in file order (empty for other sources).
  const std::vector<StepCamera>& cameras() const { return _cameras; }
  void setCameras(std::vector<StepCamera> cameras) { _cameras = std::move(cameras); }

private:
  OccHandle<TDocStd_Document> _doc;
  std::string _sourcePath;
  FileFormat _format = FileFormat::Unknown;
  std::string _fileUnitName;
  std::optional<LengthUnit> _fileUnit;
  std::vector<StepCamera> _cameras;
};

using DocumentPtr = std::shared_ptr<Document>;

// UTF-8 conversion for OCCT's wide strings (names in STEP files are often non-ASCII).
std::string toUtf8(const TCollection_ExtendedString& text);

// Maps STEP SI/conversion-based unit names ("millimetre", "INCH", ...) to a unit.
std::optional<LengthUnit> lengthUnitFromStepName(const std::string& name);

} // namespace sc::core
