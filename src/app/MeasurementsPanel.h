#pragma once

#include "core/Measurement.h"

#include <QWidget>

#include <vector>

class QLabel;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace sc::app {

// Pinned list of measurement results. Each result is a row (mode and main
// value) whose children are its details. Results stay until removed (Remove,
// the Delete key or the context menu) or until another model is opened, and
// can be copied or exported as CSV.
//
// Each successful result has a checkbox: checked results have their witness
// drawn in the 3D view, any number at once. New results start checked.
class MeasurementsPanel : public QWidget {
  Q_OBJECT

public:
  explicit MeasurementsPanel(QWidget* parent = nullptr);

  void addMeasurement(const core::Measurement& measurement);
  void clear();
  const std::vector<core::Measurement>& measurements() const { return _measurements; }

  // Stable id of the result at index (survives removal of other results).
  int idAt(int index) const { return inRange(index) ? _ids[static_cast<size_t>(index)] : -1; }
  // Whether the result at index is checked to show in the 3D view. Always false
  // for failed results, which have nothing to draw.
  bool isShown(int index) const { return inRange(index) && _shown[static_cast<size_t>(index)]; }
  void setShown(int index, bool shown);
  void setAllShown(bool shown);

  // Index of the result the user is looking at, or -1.
  int currentIndex() const;

  // Removes the selected results (or the current one when none is selected).
  void removeSelected();

  // Instruction line above the list, e.g. "Distance: pick 1 of 2".
  void setHint(const QString& text);

Q_SIGNALS:
  // A result was chosen (index into measurements()), or -1 when none is.
  void currentChanged(int index);
  // What the 3D view should draw changed: results added, removed or (un)checked,
  // or another result became current.
  void displayChanged();

private:
  void copySelected();
  void exportCsv();
  void showContextMenu(const QPoint& pos);
  void onItemChanged(QTreeWidgetItem* item, int column);
  void setShownRows(const std::vector<int>& indices, bool shown);
  bool inRange(int index) const { return index >= 0 && index < static_cast<int>(_measurements.size()); }
  std::vector<int> selectedIndices() const;
  int indexOfId(int id) const;
  bool anyShowable() const;
  bool anyShown() const;
  void updateButtons();

  QLabel* _hint = nullptr;
  QTreeWidget* _tree = nullptr;
  QToolButton* _copy = nullptr;
  QToolButton* _export = nullptr;
  QToolButton* _showAll = nullptr;
  QToolButton* _remove = nullptr;
  QToolButton* _clear = nullptr;
  std::vector<core::Measurement> _measurements;
  std::vector<int> _ids;
  std::vector<bool> _shown;
  int _nextId = 1;
  bool _updating = false;
};

} // namespace sc::app
