#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

class QLabel;
class QListWidget;
class QPlainTextEdit;

namespace sc::app {

// Help > About: application info, a system report for bug reports, and the full
// license texts of the application and the libraries it ships with.
class AboutDialog : public QDialog {
  Q_OBJECT
public:
  explicit AboutDialog(QWidget* parent = nullptr);

  // OpenGL vendor/renderer/version text supplied by the viewport. Shown in the system report.
  void setGraphicsInfo(const QString& info);

  // The system report as shown (and copied) by the dialog.
  QString systemInfo() const;

  struct LicenseEntry {
    QString title;     // list label
    QStringList files; // resource paths of the license texts, concatenated when shown
    QString sourceUrl; // where the source of the component is available
    bool replaceable;  // LGPL component: show the replaceability note
  };
  static QList<LicenseEntry> licenseEntries();
  // Concatenated text of all files of an entry; empty if any file fails to load.
  static QString licenseText(const LicenseEntry& entry);

  int licenseCount() const;
  void selectLicense(int index);
  QString shownLicenseText() const;
  void copySystemInfo();

private:
  void refreshSystemInfo();
  void showLicense(int index);

  QList<LicenseEntry> m_entries;
  QString m_graphicsInfo;
  QPlainTextEdit* m_systemText = nullptr;
  QListWidget* m_licenseList = nullptr;
  QPlainTextEdit* m_licenseText = nullptr;
  QLabel* m_licenseSource = nullptr;
  QLabel* m_licenseNote = nullptr;
};

} // namespace sc::app
