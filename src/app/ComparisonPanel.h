#pragma once

#include "core/Comparison.h"
#include "core/Measurement.h"

#include <QWidget>

#include <memory>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QToolButton;
class QTreeWidget;

namespace sc::app {

// The Comparison dock (File > Compare With, docs/designs/model-comparison.md): a summary, the
// tolerance, the parts that differ (all of them with Show Unchanged) with their change and largest
// deviation, progress with Cancel while comparing, and End Comparison.
class ComparisonPanel : public QWidget {
  Q_OBJECT

public:
  explicit ComparisonPanel(QWidget* parent = nullptr);

  void setRunning(int percent, const QString& stage);
  // A finished comparison with 'otherName' (the other file's name). Values use the units.
  void setResult(std::shared_ptr<const core::ComparisonResult> result, const QString& otherName,
                 const core::MeasureDisplayOptions& units);
  void setError(const QString& message); // the other file did not open
  void clear();

  bool isRunning() const { return _running; }
  QString summary() const;
  int rowCount() const;
  // The chosen row's index into the result's changes, or -1.
  int currentChange() const;
  void setCurrentRow(int row);
  double tolerance() const; // mm
  void setShowUnchanged(bool show);

  static QString summaryOf(const core::ComparisonResult& result, const QString& otherName);

Q_SIGNALS:
  void cancelRequested();
  void endRequested();
  void compareAgainRequested(double toleranceMm);
  void changeChosen(int changeIndex);
  void changeActivated(int changeIndex);

private:
  void fillRows();

  QLabel* _summary = nullptr;
  QProgressBar* _progress = nullptr;
  QToolButton* _cancel = nullptr;
  QDoubleSpinBox* _tolerance = nullptr;
  QToolButton* _again = nullptr;
  QCheckBox* _showUnchanged = nullptr;
  QToolButton* _end = nullptr;
  QTreeWidget* _tree = nullptr;
  std::shared_ptr<const core::ComparisonResult> _result;
  core::MeasureDisplayOptions _units;
  bool _running = false;
};

} // namespace sc::app
