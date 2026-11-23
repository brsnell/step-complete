// ComparisonPanel (File > Compare With; docs/designs/model-comparison.md): the summary, the rows
// of parts that differ, Show Unchanged, the chosen row and Compare Again.

#include "app/ComparisonPanel.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>

using sc::app::ComparisonPanel;
using sc::core::ChangeKind;

class ComparisonPanelTest : public QObject {
  Q_OBJECT

private:
  static std::shared_ptr<sc::core::ComparisonResult> sample() {
    auto result = std::make_shared<sc::core::ComparisonResult>();
    const auto add = [&result](ChangeKind kind, const std::string& path, double deviation) {
      sc::core::PartChange change;
      change.kind = kind;
      change.path = path;
      change.maxDeviation = deviation;
      result->changes.push_back(change);
    };
    add(ChangeKind::Changed, "Base plate", 2.0);
    add(ChangeKind::Unchanged, "Pin pair / Pin", 0.0);
    add(ChangeKind::Moved, "Pin pair / Pin", 0.0);
    add(ChangeKind::Removed, "Clip", 0.0);
    result->changed = 1;
    result->unchanged = 1;
    result->moved = 1;
    result->removed = 1;
    return result;
  }

private Q_SLOTS:
  void summarisesResults() {
    QCOMPARE(ComparisonPanel::summaryOf(*sample(), QStringLiteral("rev-b.step")),
             QStringLiteral("Compared with rev-b.step: 1 changed, 1 moved, 1 removed, 1 unchanged."));
    sc::core::ComparisonResult same;
    same.unchanged = 12;
    QCOMPARE(ComparisonPanel::summaryOf(same, QStringLiteral("rev-b.step")),
             QStringLiteral("No differences from rev-b.step (12 parts)."));
    same.cancelled = true;
    QCOMPARE(ComparisonPanel::summaryOf(same, QStringLiteral("x")), QStringLiteral("Comparison cancelled."));
  }

  void listsWhatDiffersAndEmitsTheChoice() {
    ComparisonPanel panel;
    panel.setRunning(20, QString());
    QVERIFY(panel.isRunning());
    sc::core::MeasureDisplayOptions units;
    units.decimals = 2;
    panel.setResult(sample(), QStringLiteral("rev-b.step"), units);
    QVERIFY(!panel.isRunning());
    QCOMPARE(panel.rowCount(), 3); // the unchanged pin is hidden
    const auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("comparisonList"));
    QCOMPARE(tree->topLevelItem(0)->text(1), QStringLiteral("Changed"));
    QCOMPARE(tree->topLevelItem(0)->text(2), QStringLiteral("2.00 mm"));
    QCOMPARE(tree->topLevelItem(2)->text(1), QStringLiteral("Removed"));

    panel.setShowUnchanged(true);
    QCOMPARE(panel.rowCount(), 4);

    QSignalSpy chosen(&panel, &ComparisonPanel::changeChosen);
    panel.setCurrentRow(2); // the moved pin: change index 2
    QCOMPARE(chosen.count(), 1);
    QCOMPARE(chosen.takeFirst().at(0).toInt(), 2);
    QCOMPARE(panel.currentChange(), 2);

    QSignalSpy again(&panel, &ComparisonPanel::compareAgainRequested);
    panel.findChild<QDoubleSpinBox*>(QStringLiteral("comparisonTolerance"))->setValue(0.5);
    for (QToolButton* button : panel.findChildren<QToolButton*>()) {
      if (button->text() == QStringLiteral("Compare Again")) {
        button->click();
      }
    }
    QCOMPARE(again.count(), 1);
    QCOMPARE(again.takeFirst().at(0).toDouble(), 0.5);

    panel.clear();
    QCOMPARE(panel.rowCount(), 0);
    panel.setError(QStringLiteral("Could not open x.stp"));
    QCOMPARE(panel.summary(), QStringLiteral("Could not open x.stp"));
  }
};

int runComparisonPanelTests(int argc, char** argv) {
  ComparisonPanelTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_comparison_panel.moc"
