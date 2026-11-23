#pragma once

#include "core/Markup.h"

#include <QWidget>

#include <functional>
#include <vector>

class QLabel;
class QListWidget;
class QToolButton;

namespace sc::app {

// The Markup dock (docs/designs/markup.md): every note (its text) and stroke ("Sketch 2") of
// the open model, with the ones whose part is missing marked. Choosing a row emphasizes the
// item, double-clicking edits a note or zooms to a stroke, Delete removes the chosen rows.
class MarkupPanel : public QWidget {
  Q_OBJECT

public:
  explicit MarkupPanel(QWidget* parent = nullptr);

  // Lists the markup; the chosen row stays on the same item when it is still there.
  void setMarkup(const core::Markup& markup, const std::function<bool(int)>& isMissing);
  void clear();

  int rowCount() const;
  QString rowText(int row) const;
  int idAt(int row) const;
  int currentId() const; // 0 when no row is chosen
  void setCurrentId(int id);
  std::vector<int> selectedIds() const;

  // One line for a note's row: its first line, "..." when there are more.
  static QString noteLabel(const std::string& text);

Q_SIGNALS:
  void itemChosen(int id); // 0: none
  void itemActivated(int id);
  void deleteRequested(); // for selectedIds()

private:
  void updateButtons();

  QLabel* _hint = nullptr;
  QListWidget* _list = nullptr;
  QToolButton* _delete = nullptr;
};

} // namespace sc::app
