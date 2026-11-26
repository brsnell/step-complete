// The Exploded View dock (docs/designs/exploded-view.md): the distance slider and Collapse.

#include "app/ExplodePanel.h"

#include <QLabel>
#include <QSignalSpy>
#include <QSlider>
#include <QTest>
#include <QToolButton>

using sc::app::ExplodePanel;

class ExplodePanelTest : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void sliderSaysTheDistanceAndCollapseFollowsTheState() {
    ExplodePanel panel;
    auto* slider = panel.findChild<QSlider*>(QStringLiteral("explodeDistance"));
    auto* value = panel.findChild<QLabel*>(QStringLiteral("explodeDistanceValue"));
    auto* collapse = panel.findChild<QToolButton*>(QStringLiteral("explodeCollapse"));
    QVERIFY(slider != nullptr);
    QVERIFY(value != nullptr);
    QVERIFY(collapse != nullptr);
    QCOMPARE(panel.distance(), ExplodePanel::DefaultDistance);
    QCOMPARE(value->text(), QStringLiteral("50 %"));

    QSignalSpy changed(&panel, &ExplodePanel::distanceChanged);
    slider->setValue(80); // as the user drags
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.takeFirst().at(0).toInt(), 80);
    QCOMPARE(value->text(), QStringLiteral("80 %"));

    panel.setDistance(20); // from the window: no signal back
    QCOMPARE(changed.count(), 0);
    QCOMPARE(panel.distance(), 20);
    QCOMPARE(value->text(), QStringLiteral("20 %"));

    QVERIFY(!collapse->isEnabled());
    panel.setExploded(true);
    QVERIFY(collapse->isEnabled());
    QSignalSpy collapsed(&panel, &ExplodePanel::collapseRequested);
    collapse->click();
    QCOMPARE(collapsed.count(), 1);
    panel.setExploded(false);
    QVERIFY(!collapse->isEnabled());

    // A model that cannot explode: the slider is off until another model comes.
    QVERIFY(slider->isEnabled());
    panel.setAvailable(false);
    QVERIFY(!slider->isEnabled());
    panel.setAvailable(true);
    QVERIFY(slider->isEnabled());
  }
};

int runExplodePanelTests(int argc, char** argv) {
  ExplodePanelTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_explode_panel.moc"
