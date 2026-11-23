// Markup (docs/designs/markup.md): ids, editing and placing items by their part.

#include "TestModels.h"
#include "core/AssemblyIndex.h"
#include "core/Markup.h"

#include <gtest/gtest.h>

using namespace sc;

TEST(Markup, IdsAreUniqueAndStable) {
  core::Markup markup;
  EXPECT_TRUE(markup.empty());
  const int note = markup.addNote({0, "Check this", "", gp_Pnt(1, 2, 3)});
  const int stroke = markup.addStroke({0, "", {gp_Pnt(0, 0, 0), gp_Pnt(1, 0, 0)}});
  EXPECT_NE(note, stroke);
  EXPECT_EQ(markup.size(), 2);
  ASSERT_NE(markup.note(note), nullptr);
  EXPECT_EQ(markup.note(note)->text, "Check this");
  EXPECT_EQ(markup.note(stroke), nullptr);
  ASSERT_NE(markup.stroke(stroke), nullptr);
  EXPECT_EQ(markup.stroke(stroke)->points.size(), 2U);

  EXPECT_TRUE(markup.remove(note));
  EXPECT_FALSE(markup.remove(note));
  const int again = markup.addNote({0, "Second", "", gp_Pnt()});
  EXPECT_NE(again, note); // never reused
  EXPECT_NE(again, stroke);
}

TEST(Markup, EditAppendClear) {
  core::Markup markup;
  const int note = markup.addNote({0, "old", "", gp_Pnt()});
  EXPECT_TRUE(markup.setNoteText(note, "line 1\nline 2"));
  EXPECT_EQ(markup.note(note)->text, "line 1\nline 2");
  EXPECT_FALSE(markup.setNoteText(note + 100, "x"));

  core::Markup other;
  other.addNote({0, "imported", "", gp_Pnt()});
  other.addStroke({0, "", {gp_Pnt(), gp_Pnt(1, 1, 1)}});
  markup.append(other);
  ASSERT_EQ(markup.notes().size(), 2U);
  EXPECT_EQ(markup.strokes().size(), 1U);
  EXPECT_NE(markup.notes()[0].id, markup.notes()[1].id);
  EXPECT_NE(markup.notes()[1].id, markup.strokes()[0].id);

  markup.clear();
  EXPECT_TRUE(markup.empty());
}

TEST(Markup, PlacementFollowsThePart) {
  const core::AssemblyIndex index(test::buildFixtureAssembly());
  // The second pin of pair #1 sits at (10, 10, 10) + (20, 0, 0).
  core::NodeId pin = core::NoNode;
  for (const core::NodeId part : index.partInstances()) {
    const gp_XYZ origin = index.node(part).location.Transformation().TranslationPart();
    if (origin.IsEqual(gp_XYZ(30, 10, 10), 1e-9)) {
      pin = part;
    }
  }
  ASSERT_NE(pin, core::NoNode);
  const std::string id = index.pathId(pin);

  const std::optional<gp_Trsf> placement = core::markupPlacement(index, id);
  ASSERT_TRUE(placement);
  const gp_Pnt world = gp_Pnt(3, 0, 20).Transformed(*placement); // top rim of the pin
  EXPECT_NEAR(world.Distance(gp_Pnt(33, 10, 30)), 0.0, 1e-9);
  // And back: world to part coordinates.
  EXPECT_NEAR(world.Transformed(placement->Inverted()).Distance(gp_Pnt(3, 0, 20)), 0.0, 1e-9);

  const std::optional<gp_Trsf> world0 = core::markupPlacement(index, "");
  ASSERT_TRUE(world0);
  EXPECT_NEAR(gp_Pnt(1, 2, 3).Transformed(*world0).Distance(gp_Pnt(1, 2, 3)), 0.0, 1e-12);
  EXPECT_FALSE(core::markupPlacement(index, "no/such/part"));
}
