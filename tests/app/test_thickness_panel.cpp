// ThicknessPanel (Measure > Wall Thickness; docs/designs/wall-thickness.md): the summary, the
// limits in the display unit, and Reset.

#include "app/ThicknessPanel.h"

#include <QDoubleSpinBox>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

using sc::app::ThicknessPanel;

class ThicknessPanelTest : public QObject {
  Q_OBJECT

private:
  static sc::core::ThicknessResult sample() {
    sc::core::ThicknessResult result;
    result.definitions = 3;
    result.withoutValue = 1;
    result.min = 1.5;
    result.max = 12.0;
    result.points = 1000;
    result.valued = 900;
    return result;
  }

private Q_SLOTS:
  void summarisesResults() {
    sc::core::MeasureDisplayOptions units;
    units.decimals = 2;
    QCOMPARE(
        ThicknessPanel::summaryOf(sample(), units),
        QStringLiteral("Wall thickness 1.50 to 12.00 mm in 2 parts. 1 part has no wall (a sheet) and is not "
                       "colored. 10.0% of the surface found no wall behind it (grey)."));
    sc::core::ThicknessResult sheets;
    sheets.definitions = 2;
    sheets.withoutValue = 2;
    QCOMPARE(ThicknessPanel::summaryOf(sheets, units),
             QStringLiteral("No wall thickness found: the parts are sheets or have no mesh."));
    sheets.cancelled = true;
    QCOMPARE(ThicknessPanel::summaryOf(sheets, units), QStringLiteral("Wall thickness cancelled."));
  }

  void limitsUseTheUnitAndReset() {
    ThicknessPanel panel;
    sc::core::MeasureDisplayOptions inches;
    inches.lengthUnit = sc::core::LengthUnit::Inch;
    inches.decimals = 3;
    panel.setRunning(50, QString());
    QVERIFY(panel.isRunning());
    panel.setResult(sample(), inches);
    QVERIFY(!panel.isRunning());
    auto* thin = panel.findChild<QDoubleSpinBox*>(QStringLiteral("thicknessThin"));
    auto* thick = panel.findChild<QDoubleSpinBox*>(QStringLiteral("thicknessThick"));
    QVERIFY(thin && thick);
    QCOMPARE(thin->suffix(), QStringLiteral(" in"));
    QCOMPARE(thin->value(), 0.059); // 1.5 mm
    QCOMPARE(panel.thickLimit(), thick->value() * 25.4);

    QSignalSpy changed(&panel, &ThicknessPanel::limitsChanged);
    thin->setValue(0.1);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.takeFirst().at(0).toDouble(), 2.54);

    for (QToolButton* button : panel.findChildren<QToolButton*>()) {
      if (button->text() == QStringLiteral("Reset")) {
        button->click();
      }
    }
    QCOMPARE(changed.count(), 1);
    QVERIFY(qAbs(changed.takeFirst().at(0).toDouble() - 1.5) < 0.01);
  }
};

int runThicknessPanelTests(int argc, char** argv) {
  ThicknessPanelTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_thickness_panel.moc"
