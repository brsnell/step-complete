// ExportGeometryDialog (File > Export Geometry): which options apply to which format, "Selected
// parts" only with a selection, and the choices remembered in QSettings.

#include "app/ExportGeometryDialog.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QRadioButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

using sc::app::ExportGeometryDialog;
namespace core = sc::core;

namespace {

void choose(QComboBox* combo, const char* value) {
  const int index = combo->findData(QString::fromLatin1(value));
  QVERIFY(index >= 0);
  combo->setCurrentIndex(index);
}

} // namespace

class ExportGeometryDialogTest : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

  void init() {
    QSettings settings;
    settings.remove(QStringLiteral("exportGeometry"));
  }

  void defaultsAreOneStlAsDisplayedInMillimetres() {
    ExportGeometryDialog dialog(false);
    const core::GeometryExportOptions options = dialog.options();
    QCOMPARE(options.format, core::GeometryFormat::StlMerged);
    QCOMPARE(options.quality, core::MeshQuality::AsDisplayed);
    QCOMPARE(options.unit, core::LengthUnit::Millimeter);
    QCOMPARE(options.objUpAxis, core::UpAxis::Y);
    QVERIFY(!dialog.selectedPartsOnly());
  }

  void optionsFollowTheFormat() {
    ExportGeometryDialog dialog(false);
    auto* format = dialog.findChild<QComboBox*>(QStringLiteral("exportFormat"));
    auto* unit = dialog.findChild<QComboBox*>(QStringLiteral("exportUnit"));
    auto* upAxis = dialog.findChild<QComboBox*>(QStringLiteral("exportUpAxis"));
    auto* quality = dialog.findChild<QComboBox*>(QStringLiteral("exportQuality"));
    auto* tolerance = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("exportTolerance"));
    QVERIFY(format && unit && upAxis && quality && tolerance);

    QVERIFY(unit->isEnabled());
    QVERIFY(!upAxis->isEnabled()); // STL keeps Z up
    choose(format, "obj");
    QVERIFY(unit->isEnabled());
    QVERIFY(upAxis->isEnabled());
    choose(unit, "in");
    QCOMPARE(dialog.options().unit, core::LengthUnit::Inch);
    choose(format, "glb");
    QVERIFY(!unit->isEnabled());
    QVERIFY(!upAxis->isEnabled());
    QCOMPARE(dialog.options().unit, core::LengthUnit::Millimeter); // glTF: tolerance in mm, file in metres

    QVERIFY(!tolerance->isEnabled());
    choose(quality, "custom");
    QVERIFY(tolerance->isEnabled());
    QCOMPARE(tolerance->suffix(), QStringLiteral(" mm"));
  }

  void selectedPartsNeedASelection() {
    ExportGeometryDialog without(false);
    auto* selected = without.findChild<QRadioButton*>(QStringLiteral("exportSelectedParts"));
    QVERIFY(selected != nullptr);
    QVERIFY(!selected->isEnabled());
    selected->setChecked(true); // even if forced, nothing is selected
    QVERIFY(!without.selectedPartsOnly());

    ExportGeometryDialog with(true);
    auto* selectedWith = with.findChild<QRadioButton*>(QStringLiteral("exportSelectedParts"));
    QVERIFY(selectedWith->isEnabled());
    selectedWith->setChecked(true);
    QVERIFY(with.selectedPartsOnly());
  }

  void choicesAreRemembered() {
    {
      ExportGeometryDialog dialog(true);
      choose(dialog.findChild<QComboBox*>(QStringLiteral("exportFormat")), "obj");
      choose(dialog.findChild<QComboBox*>(QStringLiteral("exportQuality")), "custom");
      choose(dialog.findChild<QComboBox*>(QStringLiteral("exportUnit")), "cm");
      choose(dialog.findChild<QComboBox*>(QStringLiteral("exportUpAxis")), "z");
      dialog.findChild<QDoubleSpinBox*>(QStringLiteral("exportTolerance"))->setValue(0.05);
      dialog.findChild<QDoubleSpinBox*>(QStringLiteral("exportAngle"))->setValue(4.0);
      dialog.findChild<QRadioButton*>(QStringLiteral("exportSelectedParts"))->setChecked(true);
      dialog.saveSettings();
    }
    QCOMPARE(QSettings().value(QStringLiteral("exportGeometry/format")).toString(), QStringLiteral("obj"));

    ExportGeometryDialog again(true);
    const core::GeometryExportOptions options = again.options();
    QCOMPARE(options.format, core::GeometryFormat::Obj);
    QCOMPARE(options.quality, core::MeshQuality::Custom);
    QCOMPARE(options.unit, core::LengthUnit::Centimeter);
    QCOMPARE(options.objUpAxis, core::UpAxis::Z);
    QCOMPARE(options.customTolerance, 0.05);
    QCOMPARE(options.customAngleDegrees, 4.0);
    QVERIFY(again.selectedPartsOnly());

    // Without a selection the dialog falls back to the visible parts, and keeps the saved choice.
    ExportGeometryDialog noSelection(false);
    QVERIFY(!noSelection.selectedPartsOnly());
    noSelection.saveSettings();
    QCOMPARE(QSettings().value(QStringLiteral("exportGeometry/parts")).toString(),
             QStringLiteral("selected"));
  }
};

int runExportGeometryDialogTests(int argc, char** argv) {
  ExportGeometryDialogTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_export_geometry_dialog.moc"
