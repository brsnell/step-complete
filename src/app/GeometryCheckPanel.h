#pragma once

#include "core/AssemblyIndex.h"
#include "core/GeometryCheck.h"

#include <QWidget>

#include <vector>

class QLabel;
class QProgressBar;
class QToolButton;
class QTreeWidget;

namespace sc::app {

// The Geometry Check dock (Measure > Check Geometry, docs/designs/geometry-check.md): a summary
// line, a progress bar with Cancel while the check runs, and one row per problem (part, kind,
// detail). Choosing a row asks the 3D view to highlight the problem; double-clicking zooms to it.
class GeometryCheckPanel : public QWidget {
  Q_OBJECT

public:
  explicit GeometryCheckPanel(QWidget* parent = nullptr);

  // Shows the running state: the progress bar and Cancel; the previous rows go.
  void setRunning(int percent, const QString& stage);
  // Shows a finished (or cancelled) check. The index names the parts.
  void setResult(const core::GeometryCheckResult& result, const core::AssemblyIndex& index);
  // Back to the empty panel (another model was opened or this one closed).
  void clear();

  bool isRunning() const { return _running; }
  QString summary() const;
  const std::vector<core::GeometryIssue>& issues() const { return _issues; }
  int rowCount() const;
  // The chosen row, or -1.
  int currentIndex() const;
  void setCurrentIndex(int index);

  // The summary line for a result, also shown in the status bar.
  static QString summaryOf(const core::GeometryCheckResult& result);

Q_SIGNALS:
  void cancelRequested();
  void rerunRequested();
  // A row was chosen (index into issues()), or -1 when none is.
  void issueChosen(int index);
  // A row was double-clicked: zoom to the problem.
  void issueActivated(int index);

private:
  QLabel* _summary = nullptr;
  QProgressBar* _progress = nullptr;
  QToolButton* _cancel = nullptr;
  QToolButton* _rerun = nullptr;
  QTreeWidget* _tree = nullptr;
  std::vector<core::GeometryIssue> _issues;
  bool _running = false;
};

} // namespace sc::app
