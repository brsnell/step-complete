// ClashPanel and ClashDialog (Measure > Check Clashes; docs/designs/clash-detection.md): the summary
// line, one row per pair, choosing a row, and the options remembered for next time.

#include "TestModels.h"
#include "app/ClashDialog.h"
#include "app/ClashPanel.h"
#include "core/AssemblyIndex.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

using sc::app::ClashDialog;
using sc::app::ClashPanel;

class ClashPanelTest : public QObject {
  Q_OBJECT

private:
  QTemporaryDir _settingsDir;
  QString _organization;
  QString _application;
  QSettings::Format _format = QSettings::NativeFormat;

private Q_SLOTS:
  // As in the other dialog tests: a default QSettings needs an organization name (on Windows the
  // registry writes are dropped otherwise), so use an INI file and restore the process-wide values.
  void initTestCase() {
    QVERIFY(_settingsDir.isValid());
    _organization = QCoreApplication::organizationName();
    _application = QCoreApplication::applicationName();
    _format = QSettings::defaultFormat();
    QCoreApplication::setOrganizationName(QStringLiteral("StepCompleteTest"));
    QCoreApplication::setApplicationName(QStringLiteral("clash-test"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, _settingsDir.path());
  }

  void cleanupTestCase() {
    QCoreApplication::setOrganizationName(_organization);
    QCoreApplication::setApplicationName(_application);
    QSettings::setDefaultFormat(_format);
  }

  void summarisesResults() {
    sc::core::ClashResult none;
    none.partsChecked = 45;
    none.pairsChecked = 120;
    QCOMPARE(ClashPanel::summaryOf(none), QStringLiteral("No clashes among 45 parts (120 pairs checked)."));

    sc::core::ClashResult some;
    some.partsChecked = 2;
    some.pairsChecked = 1;
    some.clashCount = 4;
    some.interferences = 3;
    some.touching = 1;
    QCOMPARE(ClashPanel::summaryOf(some),
             QStringLiteral("3 interferences, 1 touching pair among 2 parts (1 pair checked)."));
    some.cancelled = true;
    QCOMPARE(ClashPanel::summaryOf(some), QStringLiteral("Clash check cancelled."));
  }

  void listsPairsAndEmitsTheChosenOne() {
    const sc::core::AssemblyIndex index(sc::test::buildFixtureAssembly());
    const std::vector<sc::core::NodeId>& parts = index.partInstances();

    ClashPanel panel;
    panel.setRunning(10, QStringLiteral("Checking clashes"));
    QVERIFY(panel.isRunning());

    sc::core::ClashResult result;
    result.partsChecked = 3;
    result.pairsChecked = 2;
    sc::core::Clash overlap;
    overlap.a = parts[0];
    overlap.b = parts[1];
    overlap.kind = sc::core::ClashKind::Interference;
    overlap.volume = 200.0;
    sc::core::Clash gap = overlap;
    gap.kind = sc::core::ClashKind::Clearance;
    gap.volume = 0.0;
    gap.distance = 0.5;
    result.clashes = {overlap, gap};
    result.clashCount = 2;
    result.interferences = 1;
    result.clearances = 1;
    sc::core::MeasureDisplayOptions units;
    units.decimals = 2;
    panel.setResult(result, index, units);
    QVERIFY(!panel.isRunning());
    QCOMPARE(panel.rowCount(), 2);
    QCOMPARE(panel.summary(),
             QStringLiteral("1 interference, 1 pair too close among 3 parts (2 pairs checked)."));

    const auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("clashList"));
    QVERIFY(tree != nullptr);
    QCOMPARE(tree->topLevelItem(0)->text(0), QString::fromStdString(index.node(parts[0]).name));
    QCOMPARE(tree->topLevelItem(0)->text(2), QStringLiteral("Interference"));
    QVERIFY2(tree->topLevelItem(0)->text(3).startsWith(QStringLiteral("200.00 mm")),
             qPrintable(tree->topLevelItem(0)->text(3)));
    QCOMPARE(tree->topLevelItem(1)->text(3), QStringLiteral("0.50 mm gap"));

    QSignalSpy chosen(&panel, &ClashPanel::clashChosen);
    panel.setCurrentIndex(1);
    QCOMPARE(chosen.count(), 1);
    QCOMPARE(chosen.takeFirst().at(0).toInt(), 1);

    panel.clear();
    QCOMPARE(panel.rowCount(), 0);
    QVERIFY(panel.clashes().empty());
  }

  void dialogDefaultsAndRemembersItsChoices() {
    QSettings().remove(QStringLiteral("clash"));
    {
      ClashDialog dialog;
      const sc::core::ClashOptions options = dialog.options();
      QVERIFY(!options.touching);
      QVERIFY(!options.clearance);
      auto* value = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("clashClearanceValue"));
      QVERIFY(!value->isEnabled());

      dialog.findChild<QCheckBox*>(QStringLiteral("clashTouching"))->setChecked(true);
      dialog.findChild<QCheckBox*>(QStringLiteral("clashClearance"))->setChecked(true);
      QVERIFY(value->isEnabled());
      value->setValue(2.5);
      dialog.saveSettings();
    }
    ClashDialog again;
    const sc::core::ClashOptions options = again.options();
    QVERIFY(options.touching);
    QVERIFY(options.clearance);
    QCOMPARE(*options.clearance, 2.5);
  }
};

int runClashPanelTests(int argc, char** argv) {
  ClashPanelTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_clash_panel.moc"
