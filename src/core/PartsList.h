#pragma once

#include "core/AssemblyIndex.h"
#include "core/StepImporter.h"
#include "core/Units.h"

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sc::core {

// File > Export Parts List: a bill of materials, one row per part definition
// (docs/designs/parts-list-export.md).

struct PartsListOptions {
  bool volume = true;      // "Volume each"
  bool mass = true;        // "Material", "Density", "Mass each", "Mass total"
  bool boundingBox = true; // "Size X/Y/Z", the part's own axis-aligned box
  LengthUnit lengthUnit = LengthUnit::Millimeter;
  MassUnit massUnit = MassUnit::Kilogram;
  int decimals = 3;
};

struct PartsListRow {
  int item = 0;
  int quantity = 0;
  std::string part;                         // product name
  std::vector<std::string> instanceNames;   // distinct, those that differ from the product name
  std::vector<std::string> usedIn;          // distinct parent assembly paths
  std::vector<std::string> attributeValues; // by PartsList::attributeKeys, empty when unset
  std::optional<Material> material;
  std::optional<double> volume;              // mm³, one instance
  std::optional<std::array<double, 3>> size; // mm
  std::optional<double> massEach() const;    // kg: volume x density
};

struct PartsList {
  std::vector<PartsListRow> rows;
  std::vector<std::string> attributeKeys; // in order of first appearance
  bool ok = false;
  bool cancelled = false;
};

using MaterialLookup = std::function<std::optional<Material>(NodeId)>;

// Groups the given part instances (assemblies are expanded) by definition, in tree order.
// 'materialOf' gives a part's material (the user's, else the file's); it may be empty.
// The geometry columns are computed once per definition, only when their options are on.
PartsList buildPartsList(const AssemblyIndex& index, const std::vector<NodeId>& nodes,
                         const PartsListOptions& options, const MaterialLookup& materialOf = {},
                         const ProgressCallback& progress = {}, const CancelToken* cancel = nullptr);

// The CSV text (no byte-order mark; the caller writes it): a header, one line per row and a total
// line. Text from the file that a spreadsheet would run as a formula is written as text.
std::string toCsv(const PartsList& list, const PartsListOptions& options);

// A CSV field: quoted when needed; 'fromFile' text starting with = + - @ tab or CR gets a leading
// apostrophe, so spreadsheets show it instead of evaluating it.
std::string csvCell(const std::string& text, bool fromFile);

} // namespace sc::core
