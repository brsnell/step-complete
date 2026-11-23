#include "core/Markup.h"

#include <algorithm>
#include <utility>

namespace sc::core {

int Markup::addNote(MarkupNote note) {
  note.id = _nextId++;
  _notes.push_back(std::move(note));
  return _notes.back().id;
}

int Markup::addStroke(MarkupStroke stroke) {
  stroke.id = _nextId++;
  _strokes.push_back(std::move(stroke));
  return _strokes.back().id;
}

void Markup::append(const Markup& other) {
  for (const MarkupNote& note : other._notes) {
    addNote(note);
  }
  for (const MarkupStroke& stroke : other._strokes) {
    addStroke(stroke);
  }
}

const MarkupNote* Markup::note(int id) const {
  const auto it =
      std::find_if(_notes.begin(), _notes.end(), [id](const MarkupNote& n) { return n.id == id; });
  return it != _notes.end() ? &*it : nullptr;
}

const MarkupStroke* Markup::stroke(int id) const {
  const auto it =
      std::find_if(_strokes.begin(), _strokes.end(), [id](const MarkupStroke& s) { return s.id == id; });
  return it != _strokes.end() ? &*it : nullptr;
}

bool Markup::setNoteText(int id, std::string text) {
  for (MarkupNote& note : _notes) {
    if (note.id == id) {
      note.text = std::move(text);
      return true;
    }
  }
  return false;
}

bool Markup::remove(int id) {
  const auto note =
      std::find_if(_notes.begin(), _notes.end(), [id](const MarkupNote& n) { return n.id == id; });
  if (note != _notes.end()) {
    _notes.erase(note);
    return true;
  }
  const auto stroke =
      std::find_if(_strokes.begin(), _strokes.end(), [id](const MarkupStroke& s) { return s.id == id; });
  if (stroke != _strokes.end()) {
    _strokes.erase(stroke);
    return true;
  }
  return false;
}

void Markup::clear() {
  _notes.clear();
  _strokes.clear();
}

std::optional<gp_Trsf> markupPlacement(const AssemblyIndex& index, const std::string& part) {
  if (part.empty()) {
    return gp_Trsf();
  }
  const NodeId node = index.nodeForPathId(part);
  if (node == NoNode) {
    return std::nullopt;
  }
  return index.node(node).location.Transformation();
}

} // namespace sc::core
