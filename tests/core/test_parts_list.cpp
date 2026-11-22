// File > Export Parts List (docs/designs/parts-list-export.md): rows per part definition, the
// columns and units of the CSV, the total line and formula-injection guarding.

#include "TestModels.h"
#include "core/AssemblyIndex.h"
#include "core/PartsList.h"

#include <gtest/gtest.h>

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace sc;

namespace {

constexpr double Pi = 3.14159265358979323846;

std::vector<std::string> lines(const std::string& text) {
  std::vector<std::string> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    out.push_back(line);
  }
  return out;
}

core::NodeId named(const core::AssemblyIndex& index, const std::string& name) {
  for (const core::AssemblyNode& node : index.nodes()) {
    if (node.name == name) {
      return node.id;
    }
  }
  return core::NoNode;
}

} // namespace

// The fixture: a plate and two pin pairs, so 5 instances of 2 parts.
TEST(PartsList, GroupsInstancesByPartInTreeOrder) {
  const core::AssemblyIndex index(test::buildFixtureAssembly());
  const core::PartsList list = core::buildPartsList(index, index.roots(), {});
  ASSERT_TRUE(list.ok);
  ASSERT_EQ(list.rows.size(), 2U);

  const core::PartsListRow& plate = list.rows[0];
  EXPECT_EQ(plate.item, 1);
  EXPECT_EQ(plate.part, "Base plate");
  EXPECT_EQ(plate.quantity, 1);
  EXPECT_EQ(plate.usedIn, std::vector<std::string>{"Fixture"});
  ASSERT_TRUE(plate.volume);
  EXPECT_NEAR(*plate.volume, 100.0 * 60.0 * 10.0, 1e-6);
  ASSERT_TRUE(plate.size);
  EXPECT_NEAR((*plate.size)[0], 100.0, 1e-6);
  EXPECT_NEAR((*plate.size)[2], 10.0, 1e-6);

  const core::PartsListRow& pin = list.rows[1];
  EXPECT_EQ(pin.item, 2);
  EXPECT_EQ(pin.part, "Pin \xC3\x98"
                      "6");
  EXPECT_EQ(pin.quantity, 4);
  EXPECT_EQ(pin.usedIn, std::vector<std::string>{"Fixture / Pin pair"}); // both pairs, once
  EXPECT_TRUE(pin.instanceNames.empty());
  ASSERT_TRUE(pin.volume);
  EXPECT_NEAR(*pin.volume, Pi * 9.0 * 20.0, 1e-3);
  EXPECT_NEAR((*pin.size)[0], 6.0, 1e-3); // its own box, not where an instance sits

  // The plate's attributes become columns; the pin has none.
  const auto key = [&list](const std::string& name) {
    return static_cast<size_t>(std::find(list.attributeKeys.begin(), list.attributeKeys.end(), name) -
                               list.attributeKeys.begin());
  };
  ASSERT_LT(key("Part number"), list.attributeKeys.size());
  EXPECT_EQ(plate.attributeValues[key("Part number")], "BP-100");
  EXPECT_EQ(plate.attributeValues[key("Revision")], "3");
  EXPECT_EQ(pin.attributeValues[key("Part number")], "");
}

TEST(PartsList, OnlyTheChosenPartsCount) {
  const core::AssemblyIndex index(test::buildFixtureAssembly());
  // One pin pair only: two pins, no plate.
  core::NodeId pair = core::NoNode;
  for (const core::AssemblyNode& node : index.nodes()) {
    if (node.name == "Pin pair") {
      pair = node.id;
      break;
    }
  }
  ASSERT_NE(pair, core::NoNode);
  const core::PartsList list = core::buildPartsList(index, {pair}, {});
  ASSERT_EQ(list.rows.size(), 1U);
  EXPECT_EQ(list.rows[0].quantity, 2);
  EXPECT_EQ(list.rows[0].item, 1);
}

TEST(PartsList, CsvHasUnitsMassesAndATotal) {
  const core::AssemblyIndex index(test::buildFixtureAssembly());
  const core::NodeId plate = named(index, "Base plate");
  core::PartsListOptions options;
  options.decimals = 3;
  // Aluminium plate, steel pins.
  const auto materials = [&index, plate](core::NodeId part) -> std::optional<core::Material> {
    if (index.prototypeId(part) == index.prototypeId(plate)) {
      return core::Material{"Aluminium", 2700.0};
    }
    return core::Material{"Steel", 7850.0};
  };
  const core::PartsList list = core::buildPartsList(index, index.roots(), options, materials);
  const std::vector<std::string> csv = lines(core::toCsv(list, options));
  ASSERT_EQ(csv.size(), 4U); // header, 2 parts, total

  // Attribute keys sit between the names and the measured columns.
  EXPECT_EQ(csv[0], "Item,Quantity,Part,Instance names,Used in,Mass (kg),Material,Part number,Revision,"
                    "Material used,Density (kg/m\xC2\xB3),Volume each (mm\xC2\xB3),Mass each (kg),"
                    "Mass total (kg),Size X (mm),Size Y (mm),Size Z (mm)");
  EXPECT_EQ(csv[1],
            "1,1,Base plate,,Fixture,0.162000,Aluminium 6061,BP-100,3,Aluminium,2700.000,60000.000,0.162,"
            "0.162,100.000,60.000,10.000");
  // 565.487 mm³ of steel: 0.004 kg each, 0.018 kg for four.
  EXPECT_EQ(csv[2].rfind("2,4,Pin \xC3\x98"
                         "6,,Fixture / Pin pair,,,,,Steel,7850.000,565.487,0.004,0.018,6.000,6.000,20.000",
                         0),
            0U)
      << csv[2];
  EXPECT_EQ(csv[3], "Total,5,,,,,,,,,,,,0.180,,,"); // 0.162 + 4 x 0.00444

  // Grams and inches follow the Measure menu's units.
  options.massUnit = core::MassUnit::Gram;
  options.lengthUnit = core::LengthUnit::Inch;
  options.decimals = 2;
  const std::vector<std::string> imperial = lines(core::toCsv(list, options));
  EXPECT_NE(imperial[0].find("Volume each (in\xC2\xB3)"), std::string::npos);
  EXPECT_NE(imperial[0].find("Mass total (g)"), std::string::npos);
  EXPECT_NE(imperial[1].find(",162.00,162.00,3.94,2.36,0.39"), std::string::npos) << imperial[1];
}

TEST(PartsList, NoTotalMassUnlessEveryPartHasOne) {
  const core::AssemblyIndex index(test::buildFixtureAssembly());
  const core::NodeId plate = named(index, "Base plate");
  const auto plateOnly = [&index, plate](core::NodeId part) -> std::optional<core::Material> {
    if (index.prototypeId(part) == index.prototypeId(plate)) {
      return core::Material{"Aluminium", 2700.0};
    }
    return std::nullopt;
  };
  const core::PartsListOptions options;
  const std::vector<std::string> csv =
      lines(core::toCsv(core::buildPartsList(index, index.roots(), options, plateOnly), options));
  EXPECT_EQ(csv.back(), "Total,5,,,,,,,,,,,,,,,");

  // Without the optional columns nothing is measured or written.
  core::PartsListOptions bare;
  bare.volume = bare.mass = bare.boundingBox = false;
  const core::PartsList plain = core::buildPartsList(index, index.roots(), bare, plateOnly);
  EXPECT_FALSE(plain.rows[0].volume);
  EXPECT_FALSE(plain.rows[0].material);
  const std::vector<std::string> bareCsv = lines(core::toCsv(plain, bare));
  EXPECT_EQ(bareCsv[0], "Item,Quantity,Part,Instance names,Used in,Mass (kg),Material,Part number,Revision");
}

TEST(PartsList, CellsFromTheFileCannotRunAsFormulas) {
  EXPECT_EQ(core::csvCell("=HYPERLINK(\"http://x\")", true), "\"'=HYPERLINK(\"\"http://x\"\")\"");
  EXPECT_EQ(core::csvCell("+1", true), "'+1");
  EXPECT_EQ(core::csvCell("-cmd", true), "'-cmd");
  EXPECT_EQ(core::csvCell("@SUM(A1)", true), "'@SUM(A1)");
  EXPECT_EQ(core::csvCell("\tTab", true), "'\tTab");
  EXPECT_EQ(core::csvCell("Bolt, M6", true), "\"Bolt, M6\"");
  EXPECT_EQ(core::csvCell("Plain", true), "Plain");
  EXPECT_EQ(core::csvCell("-12.5", false), "-12.5"); // our own numbers stay numbers
}

TEST(PartsList, CancelStops) {
  const core::AssemblyIndex index(test::buildFixtureAssembly());
  core::CancelToken cancel;
  cancel.cancel();
  const core::PartsList list = core::buildPartsList(index, index.roots(), {}, {}, {}, &cancel);
  EXPECT_FALSE(list.ok);
  EXPECT_TRUE(list.cancelled);
}
