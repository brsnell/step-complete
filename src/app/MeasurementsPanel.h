#pragma once

#include "core/Measurement.h"

#include <QWidget>

#include <vector>

class QLabel;
class QToolButton;
class QTreeWidget;

namespace sc::app {

// Pinned list of measurement results. Each result is a row (mode and main
// value) whose children are its details. Results stay until removed, survive
// hide/show of parts, and can be copied or exported as CSV.
class MeasurementsPanel : public QWidget {
  Q_OBJECT

public:
  explicit MeasurementsPanel(QWidget* parent = nullptr);

  void addMeasurement(const core::Measurement& measurement);
  void clear();
  const std::vector<core::Measurement>& measurements() const { return m_measurements; }

  // Index of the result the user is looking at, or -1.
  int currentIndex() const;

  // Instruction line above the list, e.g. "Distance: pick 1 of 2".
  void setHint(const QString& text);

Q_SIGNALS:
  // A result was chosen (index into measurements()), or -1 when none is.
  void currentChanged(int index);

private:
  void copySelected();
  void exportCsv();
  void removeSelected();
  void updateButtons();

  QLabel* m_hint = nullptr;
  QTreeWidget* m_tree = nullptr;
  QToolButton* m_copy = nullptr;
  QToolButton* m_export = nullptr;
  QToolButton* m_remove = nullptr;
  QToolButton* m_clear = nullptr;
  std::vector<core::Measurement> m_measurements;
};

} // namespace sc::app
