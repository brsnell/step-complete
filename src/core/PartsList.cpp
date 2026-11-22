#include "core/PartsList.h"

#include "core/Measure.h"

#include <algorithm>
#include <map>

namespace sc::core {

namespace {

void addDistinct(std::vector<std::string>& list, const std::string& value) {
  if (!value.empty() && std::find(list.begin(), list.end(), value) == list.end()) {
    list.push_back(value);
  }
}

std::string joined(const std::vector<std::string>& values) {
  std::string out;
  for (const std::string& value : values) {
    out += (out.empty() ? "" : "; ") + value;
  }
  return out;
}

std::string unitSuffix(LengthUnit unit, const char* power = "") {
  return " (" + std::string(symbol(unit)) + power + ")";
}

} // namespace

std::optional<double> PartsListRow::massEach() const {
  if (!volume || !material || material->density <= 0.0) {
    return std::nullopt;
  }
  return *volume * material->density * 1e-9; // mm³ x kg/m³ -> kg
}

PartsList buildPartsList(const AssemblyIndex& index, const std::vector<NodeId>& nodes,
                         const PartsListOptions& options, const MaterialLookup& materialOf,
                         const ProgressCallback& progress, const CancelToken* cancel) {
  PartsList list;
  // The chosen part instances, in tree order, each once.
  std::vector<char> chosen(index.nodes().size(), 0);
  for (const NodeId node : nodes) {
    if (node >= 0 && static_cast<size_t>(node) < index.nodes().size()) {
      for (const NodeId part : index.partsBelow(node)) {
        chosen[static_cast<size_t>(part)] = 1;
      }
    }
  }

  std::map<std::string, size_t> rowOf; // by definition
  std::vector<NodeId> firstInstance;
  std::vector<std::vector<std::pair<std::string, std::string>>> attributes;
  for (const NodeId part : index.partInstances()) {
    if (chosen[static_cast<size_t>(part)] == 0) {
      continue;
    }
    const AssemblyNode& node = index.node(part);
    const std::string definition = index.prototypeId(part);
    auto it = rowOf.find(definition);
    if (it == rowOf.end()) {
      it = rowOf.emplace(definition, list.rows.size()).first;
      PartsListRow row;
      row.item = static_cast<int>(list.rows.size()) + 1;
      row.part = node.prototypeName.empty() ? node.name : node.prototypeName;
      list.rows.push_back(std::move(row));
      firstInstance.push_back(part);
      attributes.push_back(index.attributes(part));
      for (const auto& [key, value] : attributes.back()) {
        addDistinct(list.attributeKeys, key);
      }
    }
    PartsListRow& row = list.rows[it->second];
    ++row.quantity;
    if (node.name != row.part) {
      addDistinct(row.instanceNames, node.name);
    }
    if (node.parent != NoNode) {
      addDistinct(row.usedIn, index.displayPath(node.parent));
    }
  }

  const bool geometry = options.volume || options.mass || options.boundingBox;
  for (size_t i = 0; i < list.rows.size(); ++i) {
    if (cancel != nullptr && cancel->isCancelled()) {
      list.cancelled = true;
      return list;
    }
    if (progress) {
      progress(static_cast<double>(i) / static_cast<double>(list.rows.size()), "Measuring parts");
    }
    PartsListRow& row = list.rows[i];
    const NodeId part = firstInstance[i];
    // Attribute values by key: the first occurrence wins (instance attributes come first).
    row.attributeValues.assign(list.attributeKeys.size(), std::string());
    for (const auto& [key, value] : attributes[i]) {
      const auto keyIt = std::find(list.attributeKeys.begin(), list.attributeKeys.end(), key);
      std::string& cell = row.attributeValues[static_cast<size_t>(keyIt - list.attributeKeys.begin())];
      if (cell.empty()) {
        cell = value;
      }
    }
    if (options.mass && materialOf) {
      row.material = materialOf(part);
    }
    if (geometry) {
      // The part's own coordinates: the size does not depend on where an instance sits.
      const measure::MassProperties props = measure::massProperties({index.prototypeShape(part)});
      if (props.ok) {
        if (props.hasVolume) {
          row.volume = props.volume;
        }
        row.size =
            std::array<double, 3>{props.boxMax.X() - props.boxMin.X(), props.boxMax.Y() - props.boxMin.Y(),
                                  props.boxMax.Z() - props.boxMin.Z()};
      }
    }
  }
  if (progress) {
    progress(1.0, "Measuring parts");
  }
  list.ok = true;
  return list;
}

std::string csvCell(const std::string& text, bool fromFile) {
  std::string value = text;
  if (fromFile && !value.empty() && std::string("=+-@\t\r").find(value.front()) != std::string::npos) {
    value.insert(value.begin(), '\'');
  }
  if (value.find_first_of(",\"\n\r") == std::string::npos) {
    return value;
  }
  std::string quoted = "\"";
  for (const char character : value) {
    if (character == '"') {
      quoted += '"';
    }
    quoted += character;
  }
  return quoted + "\"";
}

std::string toCsv(const PartsList& list, const PartsListOptions& options) {
  const auto number = [&options](double value) { return formatNumber(value, options.decimals); };
  const double perUnit = millimetersPer(options.lengthUnit);
  const std::string massSymbol(symbol(options.massUnit));

  std::vector<std::string> header{"Item", "Quantity", "Part", "Instance names", "Used in"};
  for (const std::string& key : list.attributeKeys) {
    header.push_back(key);
  }
  if (options.mass) {
    header.insert(header.end(), {"Material used", "Density (kg/m\xC2\xB3)"});
  }
  if (options.volume) {
    header.push_back("Volume each" + unitSuffix(options.lengthUnit, "\xC2\xB3"));
  }
  if (options.mass) {
    header.insert(header.end(), {"Mass each (" + massSymbol + ")", "Mass total (" + massSymbol + ")"});
  }
  if (options.boundingBox) {
    for (const char* axis : {"Size X", "Size Y", "Size Z"}) {
      header.push_back(std::string(axis) + unitSuffix(options.lengthUnit));
    }
  }

  std::string csv;
  const auto writeLine = [&csv](const std::vector<std::string>& cells) {
    for (size_t i = 0; i < cells.size(); ++i) {
      csv += (i == 0 ? "" : ",") + cells[i];
    }
    csv += "\n";
  };
  std::vector<std::string> cells;
  for (const std::string& title : header) {
    cells.push_back(csvCell(title, true)); // attribute keys come from the file
  }
  writeLine(cells);

  int totalQuantity = 0;
  double totalMass = 0.0;
  bool everyMass = options.mass && !list.rows.empty();
  for (const PartsListRow& row : list.rows) {
    cells = {std::to_string(row.item), std::to_string(row.quantity), csvCell(row.part, true),
             csvCell(joined(row.instanceNames), true), csvCell(joined(row.usedIn), true)};
    for (const std::string& value : row.attributeValues) {
      cells.push_back(csvCell(value, true));
    }
    const std::optional<double> massEach = row.massEach();
    if (options.mass) {
      cells.push_back(row.material ? csvCell(row.material->name, true) : std::string());
      cells.push_back(row.material ? number(row.material->density) : std::string());
    }
    if (options.volume) {
      cells.push_back(row.volume ? number(*row.volume / (perUnit * perUnit * perUnit)) : std::string());
    }
    if (options.mass) {
      const double perMassUnit = kilogramsPer(options.massUnit);
      cells.push_back(massEach ? number(*massEach / perMassUnit) : std::string());
      cells.push_back(massEach ? number(*massEach * row.quantity / perMassUnit) : std::string());
    }
    if (options.boundingBox) {
      for (int axis = 0; axis < 3; ++axis) {
        cells.push_back(row.size ? number((*row.size)[static_cast<size_t>(axis)] / perUnit) : std::string());
      }
    }
    writeLine(cells);
    totalQuantity += row.quantity;
    if (massEach) {
      totalMass += *massEach * row.quantity;
    } else {
      everyMass = false;
    }
  }

  // Total: the quantity, and the mass when every row has one (an incomplete total would mislead).
  cells.assign(header.size(), std::string());
  cells[0] = "Total";
  cells[1] = std::to_string(totalQuantity);
  if (options.mass && everyMass) {
    const auto it = std::find(header.begin(), header.end(), "Mass total (" + massSymbol + ")");
    cells[static_cast<size_t>(it - header.begin())] = number(totalMass / kilogramsPer(options.massUnit));
  }
  writeLine(cells);
  return csv;
}

} // namespace sc::core
