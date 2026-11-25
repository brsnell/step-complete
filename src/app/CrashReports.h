#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace sc::app {

// The crash reports in a folder (docs/designs/crash-reporting.md): crash-<UTC time>.txt files,
// each with a .dmp beside it on Windows. Names sort by time.
class CrashReports {
public:
  explicit CrashReports(QString folder);
  // <AppData>/crashes
  static QString defaultFolder();
  const QString& folder() const { return _folder; }

  // Full paths of the reports, newest first.
  QStringList reports() const;
  // The reports newer than the last one shown, newest first.
  QStringList newReports() const;
  // Remembers 'report' (and everything older) as shown.
  void markSeen(const QString& report);
  // Keeps the newest 'keep' reports and removes the rest with their minidumps.
  void prune(int keep = 10);

  // The minidump beside a report, or empty when there is none.
  static QString dumpFor(const QString& report);

  // A GitHub "new issue" link with the report in its body, cut to fit a URL.
  static QUrl issueUrl(const QString& reportText, bool hasDump);
  static constexpr int MaxIssueBody = 6000;

private:
  QString lastSeen() const;
  QString _folder;
};

// "STEP Complete closed unexpectedly last time": the report (read-only), Report on GitHub,
// Copy Report, Open Folder and Close.
class CrashReportDialog : public QDialog {
  Q_OBJECT

public:
  // 'earlier': how many more new reports there are besides this one.
  CrashReportDialog(const QString& reportPath, int earlier, QWidget* parent = nullptr);

  QString reportText() const { return _text; }
  QUrl issueUrl() const;

private:
  QString _path;
  QString _text;
};

} // namespace sc::app
