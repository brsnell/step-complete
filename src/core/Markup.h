#pragma once

#include "core/AssemblyIndex.h"

#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <optional>
#include <string>
#include <vector>

namespace sc::core {

// Review markup pinned to a model (docs/designs/markup.md). Never written into the model file.
//
// Each item is anchored to a part instance by its stable id (AssemblyIndex::pathId) and keeps
// its points in that part's own coordinates, so it follows the part if a revision moves it. An
// empty part id means world coordinates (a stroke started over empty space).

// A text note pinned to a point on a part.
struct MarkupNote {
  int id = 0;
  std::string text; // may hold several lines ('\n')
  std::string part;
  gp_Pnt point;
};

// A freehand stroke.
struct MarkupStroke {
  int id = 0;
  std::string part;
  std::vector<gp_Pnt> points;
};

// The notes and strokes of one model. Ids are unique across both and never reused while the
// object lives, so a panel row keeps pointing at the same item.
class Markup {
public:
  const std::vector<MarkupNote>& notes() const { return _notes; }
  const std::vector<MarkupStroke>& strokes() const { return _strokes; }
  bool empty() const { return _notes.empty() && _strokes.empty(); }
  int size() const { return static_cast<int>(_notes.size() + _strokes.size()); }

  // Add a copy with a new id; returns the id.
  int addNote(MarkupNote note);
  int addStroke(MarkupStroke stroke);
  // Adds every item of 'other' with new ids.
  void append(const Markup& other);

  const MarkupNote* note(int id) const;
  const MarkupStroke* stroke(int id) const;
  bool setNoteText(int id, std::string text);
  // Removes the note or stroke; false if there is none with this id.
  bool remove(int id);
  void clear();

private:
  std::vector<MarkupNote> _notes;
  std::vector<MarkupStroke> _strokes;
  int _nextId = 1;
};

// Where markup anchored to 'part' is placed in the world: the part instance's placement, the
// identity for an empty id, or nothing when the model has no such part.
std::optional<gp_Trsf> markupPlacement(const AssemblyIndex& index, const std::string& part);

} // namespace sc::core
