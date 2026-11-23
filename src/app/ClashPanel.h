#pragma once

#include "core/AssemblyIndex.h"
#include "core/Clash.h"
#include "core/Measurement.h"

#include <QWidget>

#include <vector>

class QLabel;
class QProgressBar;
class QToolButton;
class QTreeWidget;

namespace sc::app {

// The Clashes dock (Measure > Check Clashes, docs/designs/clash-detection.md): a summary line, a
// progress bar with Cancel while the check runs, and one row per pair (both parts, the kind and
// the overlap volume or gap). Choosing a row asks the window to select and highlight the pair;
// double-clicking zooms to it.
class ClashPanel : public QWidget {
  Q_OBJECT

public:
  explicit ClashPanel(QWidget* parent = nullptr);

  void setRunning(int percent, const QString& stage);
  // Shows a finished (or cancelled) check. The index names the parts; values use the units.
  void setResult(const core::ClashResult& result, const core::AssemblyIndex& index,
                 const core::MeasureDisplayOptions& units);
  void clear();

  bool isRunning() const { return _running; }
  QString summary() const;
  const std::vector<core::Clash>& clashes() const { return _clashes; }
  int rowCount() const;
  int currentIndex() const;
  void setCurrentIndex(int index);

  // The summary line for a result, also shown in the status bar.
  static QString summaryOf(const core::ClashResult& result);

Q_SIGNALS:
  void cancelRequested();
  void rerunRequested();
  // A row was chosen (index into clashes()), or -1 when none is.
  void clashChosen(int index);
  // A row was double-clicked: zoom to the pair.
  void clashActivated(int index);

private:
  QLabel* _summary = nullptr;
  QProgressBar* _progress = nullptr;
  QToolButton* _cancel = nullptr;
  QToolButton* _rerun = nullptr;
  QTreeWidget* _tree = nullptr;
  std::vector<core::Clash> _clashes;
  bool _running = false;
};

} // namespace sc::app
