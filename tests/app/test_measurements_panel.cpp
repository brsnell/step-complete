// MeasurementsPanel: removing results, showing several in the 3D view at once
// (checkboxes), and clearing. Offscreen, no viewport.

#include "app/MeasurementsPanel.h"

#include <QApplication>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
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

  // Generated by /ship test coverage audit.
  // Value: protects=after a removal, a row's checkbox toggles that row's own result (id lookup, not
  // stale index); fails_when=onItemChanged/currentIndex map rows by a stored index again;
  // why_new=existing cases toggle checkboxes only before any removal; seam=none; no_claim=3D view sync
  void checkboxAfterRemovalTogglesItsOwnResult() {
    MeasurementsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    panel.addMeasurement(result("1 mm"));
    panel.addMeasurement(result("2 mm"));
    panel.addMeasurement(result("3 mm"));
    tree->clearSelection();
    tree->setCurrentItem(tree->topLevelItem(0));
    panel.removeSelected();
    QCOMPARE(headlines(panel), (std::vector<std::string>{"2 mm", "3 mm"}));

    tree->topLevelItem(1)->setCheckState(0, Qt::Unchecked); // the "3 mm" row
    QVERIFY(panel.isShown(0));
    QVERIFY2(!panel.isShown(1), "the checkbox toggled a different result");
    tree->setCurrentItem(tree->topLevelItem(1));
    QCOMPARE(panel.currentIndex(), 1);
  }

  // Generated by /ship test coverage audit.
  // Value: protects=failed results can never be shown and Show/Hide All skips them; fails_when=the
  // hasWitness guard in setShown/setAllShown is dropped or the toggle is enabled with nothing to show;
  // why_new=existing cases only check a failure's initial state; seam=none; no_claim=context menu
  void failedResultsStayHidden() {
    MeasurementsPanel panel;
    panel.addMeasurement(failure());
    QToolButton* toggle = button(panel, QStringLiteral("Show All"));
    QVERIFY2(toggle != nullptr && !toggle->isEnabled(), "nothing to show, toggle must be disabled");
    QSignalSpy display(&panel, &MeasurementsPanel::displayChanged);
    panel.setShown(0, true);
    panel.setAllShown(true);
    QVERIFY(!panel.isShown(0));
    QCOMPARE(display.count(), 0);

    panel.addMeasurement(result("1 mm"));
    QVERIFY(toggle->isEnabled());
    QCOMPARE(toggle->text(), QStringLiteral("Hide All"));
    display.clear();
    panel.setAllShown(true); // already shown: no change, no redraw request
    QCOMPARE(display.count(), 0);
    panel.setAllShown(false);
    panel.setAllShown(true);
    QVERIFY(panel.isShown(1));
    QVERIFY(!panel.isShown(0));
  }

  // Generated by /ship test coverage audit.
  // Value: protects=choosing another row requests a redraw (moves the emphasis) and reports it;
  // fails_when=currentItemChanged stops emitting displayChanged or the m_updating guard sticks;
  // why_new=no case changes the current row and spies the signals; seam=none; no_claim=MainWindow wiring
  void choosingAnotherResultRequestsARedraw() {
    MeasurementsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    panel.addMeasurement(result("1 mm"));
    panel.addMeasurement(result("2 mm"));
    QSignalSpy current(&panel, &MeasurementsPanel::currentChanged);
    QSignalSpy display(&panel, &MeasurementsPanel::displayChanged);
    tree->setCurrentItem(tree->topLevelItem(0)->child(0)); // a detail row counts as its result
    QCOMPARE(current.count(), 1);
    QCOMPARE(current.front().front().toInt(), 0);
    QCOMPARE(display.count(), 1);
  }

  // Generated by /ship test coverage audit.
  // Value: protects=only an unmodified Delete/Backspace removes results; fails_when=the NoModifier check
  // in DeleteKeyFilter is dropped; why_new=deleteKeyRemovesTheCurrentResult sends plain keys only;
  // seam=none; no_claim=platform key mapping beyond Qt key codes
  void modifiedDeleteKeysDoNotRemove() {
    MeasurementsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    panel.addMeasurement(result("1 mm"));
    QTest::keyClick(tree, Qt::Key_Delete, Qt::ShiftModifier);
    QTest::keyClick(tree, Qt::Key_Backspace, Qt::ControlModifier);
    QCOMPARE(panel.measurements().size(), size_t(1));
  }

  // Generated by /ship test coverage audit.
  // Value: protects=context menu enables "Show in 3D View" only for showable selections and toggling it
  // checks every selected row; fails_when=the menu acts on the clicked row only, or its enable/checked state
  // is wrong; why_new=no test opened showContextMenu; seam=none; no_claim=real mouse right-click delivery
  // (signal emitted directly; offscreen)
  void contextMenuShowsAndHidesTheSelection() {
    MeasurementsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    panel.addMeasurement(result("1 mm"));
    panel.addMeasurement(result("2 mm"));
    panel.addMeasurement(failure());
    panel.setShown(1, false);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    // Rows 0 and 1 selected, one of them hidden: the action is offered unchecked.
    tree->clearSelection();
    tree->topLevelItem(0)->setSelected(true);
    tree->topLevelItem(1)->setSelected(true);
    QStringList enabled;
    bool showChecked = true;
    auto inspect = [&](bool trigger) {
      QTimer::singleShot(0, [&, trigger] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (menu == nullptr) {
          return;
        }
        for (QAction* a : menu->actions()) {
          if (!a->isSeparator() && a->isEnabled()) {
            enabled << a->text();
          }
          if (a->text() == QStringLiteral("Show in 3D View")) {
            showChecked = a->isChecked();
            if (trigger) {
              a->trigger();
            }
          }
        }
        menu->close();
      });
    };
    const QPoint row0 = tree->visualItemRect(tree->topLevelItem(0)).center();
    inspect(true);
    Q_EMIT tree->customContextMenuRequested(row0);
    QVERIFY(enabled.contains(QStringLiteral("Show in 3D View")));
    QVERIFY2(!showChecked, "one selected row is hidden, so the action starts unchecked");
    QVERIFY(enabled.contains(QStringLiteral("Show All")));
    QVERIFY(enabled.contains(QStringLiteral("Hide All")));
    QVERIFY(panel.isShown(0));
    QVERIFY2(panel.isShown(1), "toggling applies to every selected row, not only the clicked one");
    QVERIFY(!panel.isShown(2));

    // Right-clicking an unselected failed row: nothing to show, but it can be copied.
    enabled.clear();
    showChecked = true;
    const QPoint row2 = tree->visualItemRect(tree->topLevelItem(2)).center();
    QTest::mouseClick(tree->viewport(), Qt::RightButton, {},
                      row2); // the press selects the row, as for a user
    inspect(false);
    Q_EMIT tree->customContextMenuRequested(row2);
    QCOMPARE(panel.currentIndex(), 2);
    QVERIFY(enabled.contains(QStringLiteral("Copy")));
    QVERIFY(enabled.contains(QStringLiteral("Remove")));
    QVERIFY2(!enabled.contains(QStringLiteral("Show in 3D View")), "a failed result has nothing to draw");
    QVERIFY(!showChecked);
    QVERIFY(enabled.contains(QStringLiteral("Clear All")));
  }
};

int runMeasurementsPanelTests(int argc, char** argv) {
  MeasurementsPanelTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_measurements_panel.moc"
