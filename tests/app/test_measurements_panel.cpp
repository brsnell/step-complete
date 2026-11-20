// MeasurementsPanel: removing results, showing several in the 3D view at once
// (checkboxes), and clearing. Offscreen, no viewport.

#include "app/MeasurementsPanel.h"

#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>

using sc::app::MeasurementsPanel;
using sc::core::Measurement;
using sc::core::MeasureMode;

namespace {

Measurement result(const std::string& headline) {
  Measurement m;
  m.mode = MeasureMode::Distance;
  m.ok = true;
  m.headline = headline;
  m.details = {{"Distance", headline}};
  m.witness.markers = {gp_Pnt(0, 0, 0)};
  m.witness.label = headline;
  return m;
}

Measurement failure() {
  Measurement m;
  m.mode = MeasureMode::Angle;
  m.error = "Pick two edges or faces";
  return m;
}

std::vector<std::string> headlines(const MeasurementsPanel& panel) {
  std::vector<std::string> out;
  for (const Measurement& m : panel.measurements()) {
    out.push_back(m.ok ? m.headline : m.error);
  }
  return out;
}

QToolButton* button(const MeasurementsPanel& panel, const QString& text) {
  for (QToolButton* b : panel.findChildren<QToolButton*>()) {
    if (b->text() == text) {
      return b;
    }
  }
  return nullptr;
}

} // namespace

class MeasurementsPanelTest : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void newResultsAreShownAndOlderOnesStayExpanded() {
    MeasurementsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    panel.addMeasurement(result("1 mm"));
    panel.addMeasurement(result("2 mm"));
    panel.addMeasurement(failure());
    QCOMPARE(panel.measurements().size(), size_t(3));
    QVERIFY(panel.isShown(0));
    QVERIFY(panel.isShown(1));
    QVERIFY2(!panel.isShown(2), "a failed result has nothing to draw");
    QCOMPARE(tree->topLevelItem(0)->checkState(0), Qt::Checked);
    QVERIFY((tree->topLevelItem(2)->flags() & Qt::ItemIsUserCheckable) == 0);
    QVERIFY2(tree->topLevelItem(0)->isExpanded(), "older results are no longer collapsed");
    QCOMPARE(panel.currentIndex(), 2);
    QVERIFY(panel.idAt(0) != panel.idAt(1));
  }

  void checkboxesToggleWhatIsShown() {
    MeasurementsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    panel.addMeasurement(result("1 mm"));
    panel.addMeasurement(result("2 mm"));
    QSignalSpy display(&panel, &MeasurementsPanel::displayChanged);

    tree->topLevelItem(0)->setCheckState(0, Qt::Unchecked); // as a click on the box does
    QVERIFY(!panel.isShown(0));
    QVERIFY(panel.isShown(1));
    QCOMPARE(display.count(), 1);

    panel.setShown(0, true);
    QVERIFY(panel.isShown(0));
    QCOMPARE(tree->topLevelItem(0)->checkState(0), Qt::Checked);

    QToolButton* toggle = button(panel, QStringLiteral("Hide All"));
    QVERIFY(toggle != nullptr);
    toggle->click();
    QVERIFY(!panel.isShown(0));
    QVERIFY(!panel.isShown(1));
    QCOMPARE(toggle->text(), QStringLiteral("Show All"));
    toggle->click();
    QVERIFY(panel.isShown(0));
    QVERIFY(panel.isShown(1));
  }

  void removeKeepsOtherRowsAndIds() {
    MeasurementsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    panel.addMeasurement(result("1 mm"));
    panel.addMeasurement(result("2 mm"));
    panel.addMeasurement(result("3 mm"));
    panel.addMeasurement(result("4 mm"));
    const int id3 = panel.idAt(2);
    tree->topLevelItem(2)->setExpanded(false);
    panel.setShown(3, false);

    // Select rows 1 and 3 (a child row counts as its result).
    tree->clearSelection();
    tree->topLevelItem(1)->setSelected(true);
    tree->topLevelItem(3)->child(0)->setSelected(true);
    QSignalSpy display(&panel, &MeasurementsPanel::displayChanged);
    panel.removeSelected();

    QCOMPARE(headlines(panel), (std::vector<std::string>{"1 mm", "3 mm"}));
    QCOMPARE(panel.idAt(1), id3);
    QCOMPARE(tree->topLevelItemCount(), 2);
    QVERIFY2(!tree->topLevelItem(1)->isExpanded(), "remaining rows keep their expansion");
    QVERIFY2(tree->topLevelItem(0)->isExpanded(), "remaining rows keep their expansion");
    QCOMPARE(panel.currentIndex(), 1); // the row that took the first removed one's place
    QCOMPARE(display.count(), 1);
  }

  void deleteKeyRemovesTheCurrentResult() {
    MeasurementsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    panel.addMeasurement(result("1 mm"));
    panel.addMeasurement(result("2 mm"));
    panel.addMeasurement(result("3 mm"));
    tree->setCurrentItem(tree->topLevelItem(0));

    QTest::keyClick(tree, Qt::Key_Delete);
    QCOMPARE(headlines(panel), (std::vector<std::string>{"2 mm", "3 mm"}));
    QTest::keyClick(tree, Qt::Key_Backspace); // the Mac delete key
    QCOMPARE(headlines(panel), (std::vector<std::string>{"3 mm"}));
    QTest::keyClick(tree, Qt::Key_Delete);
    QVERIFY(panel.measurements().empty());
    QCOMPARE(panel.currentIndex(), -1);
    QTest::keyClick(tree, Qt::Key_Delete); // nothing left: no-op
    QVERIFY(panel.measurements().empty());
  }

  void removeButtonRemovesTheSelection() {
    MeasurementsPanel panel;
    panel.addMeasurement(result("1 mm"));
    panel.addMeasurement(result("2 mm"));
    QToolButton* remove = button(panel, QStringLiteral("Remove"));
    QVERIFY(remove != nullptr);
    QVERIFY(remove->isEnabled());
    remove->click(); // the newest result is current and selected
    QCOMPARE(headlines(panel), (std::vector<std::string>{"1 mm"}));
  }

  void clearEmptiesTheList() {
    MeasurementsPanel panel;
    panel.addMeasurement(result("1 mm"));
    panel.addMeasurement(failure());
    QSignalSpy current(&panel, &MeasurementsPanel::currentChanged);
    QSignalSpy display(&panel, &MeasurementsPanel::displayChanged);
    panel.clear();
    QVERIFY(panel.measurements().empty());
    QCOMPARE(panel.findChild<QTreeWidget*>()->topLevelItemCount(), 0);
    QCOMPARE(current.count(), 1);
    QCOMPARE(current.front().front().toInt(), -1);
    QCOMPARE(display.count(), 1);
    QVERIFY(!button(panel, QStringLiteral("Clear"))->isEnabled());

    // Ids stay unique across a clear, so the 3D view never confuses old and new results.
    panel.addMeasurement(result("5 mm"));
    QVERIFY(panel.idAt(0) > 2);
  }
};

int runMeasurementsPanelTests(int argc, char** argv) {
  MeasurementsPanelTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_measurements_panel.moc"
