// PartsListDialog (File > Export Parts List; docs/designs/parts-list-export.md): defaults, and the
// choices remembered for next time.

#include "app/PartsListDialog.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QRadioButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using sc::app::PartsListDialog;

class PartsListDialogTest : public QObject {
  Q_OBJECT

private:
  QTemporaryDir _settingsDir;
  QString _organization;
  QString _application;
  QSettings::Format _format = QSettings::NativeFormat;

private Q_SLOTS:
  // As in the export dialog test: a default QSettings needs an organization name (on Windows the
  // registry writes are dropped otherwise), so use an INI file and restore the process-wide values.
  void initTestCase() {
    QVERIFY(_settingsDir.isValid());
    _organization = QCoreApplication::organizationName();
    _application = QCoreApplication::applicationName();
    _format = QSettings::defaultFormat();
    QCoreApplication::setOrganizationName(QStringLiteral("StepCompleteTest"));
    QCoreApplication::setApplicationName(QStringLiteral("parts-list-test"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, _settingsDir.path());
  }

  void cleanupTestCase() {
    QCoreApplication::setOrganizationName(_organization);
    QCoreApplication::setApplicationName(_application);
    QSettings::setDefaultFormat(_format);
  }

  void init() { QSettings().clear(); }

  void defaultsAreAllPartsWithEveryColumn() {
    PartsListDialog dialog;
    QVERIFY(!dialog.visibleOnly());
    QVERIFY(dialog.volume());
    QVERIFY(dialog.mass());
    QVERIFY(dialog.boundingBox());
  }

  void choicesAreRemembered() {
    {
      PartsListDialog dialog;
      dialog.findChild<QRadioButton*>(QStringLiteral("partsListVisible"))->setChecked(true);
      dialog.findChild<QCheckBox*>(QStringLiteral("partsListVolume"))->setChecked(false);
      dialog.findChild<QCheckBox*>(QStringLiteral("partsListBoundingBox"))->setChecked(false);
      dialog.saveSettings();
    }
    PartsListDialog again;
    QVERIFY(again.visibleOnly());
    QVERIFY(!again.volume());
    QVERIFY(again.mass());
    QVERIFY(!again.boundingBox());
  }
};

int runPartsListDialogTests(int argc, char** argv) {
  PartsListDialogTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_parts_list_dialog.moc"
