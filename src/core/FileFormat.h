#pragma once

#include "core/Units.h"

#include <optional>
#include <string>

namespace sc::core {

// The CAD formats the importer reads (docs/designs/iges-import.md).
enum class FileFormat {
  Unknown,
  Step, // ISO 10303-21: AP203, AP214, AP242
  Iges, // IGES 5.x
};

const char* displayName(FileFormat format); // "STEP", "IGES", "Unknown"

// The format of a file from its first bytes (a STEP header, or IGES's fixed 80-column records),
// else from the extension of 'nameForExtension' (the file's own path when empty). Reads at most
// a few kilobytes. Compressed files are Unknown by content: inflate them first.
FileFormat detectFileFormat(const std::string& utf8Path, const std::string& nameForExtension = {});

// The format a file name suggests: .step/.stp/.p21/.stpz (and .gz of those) are STEP,
// .igs/.iges are IGES.
FileFormat formatFromExtension(const std::string& utf8Path);

// IGES global section unit flag (parameter 14) to a length unit; nullopt for units the viewer
// has no name for (miles, kilometres, mils, microinches) or an unknown flag.
std::optional<LengthUnit> lengthUnitFromIgesFlag(int flag);

} // namespace sc::core
