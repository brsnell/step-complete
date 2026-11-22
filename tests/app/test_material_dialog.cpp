// MaterialDialog (Measure > Set Material; docs/designs/material-density.md): presets fill in
// the name and density, typing makes a custom material, and the current material is preselected.

#include "app/MaterialDialog.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QTest>

using sc::app::MaterialDialog;

class MaterialDialogTest : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void presetsFillInNameAndDensity() {
    MaterialDialog dialog;
    auto* preset = dialog.findChild<QComboBox*>(QStringLiteral("materialPreset"));
    QVERIFY(preset != nullptr);
    QCOMPARE(preset->count(), static_cast<int>(MaterialDialog::presets().size()) + 1); // + Custom
    QCOMPARE(dialog.material().name, std::string("Steel"));                            // the first preset
    QCOMPARE(dialog.material().density, 7850.0);
    for (int i = 0; i < static_cast<int>(MaterialDialog::presets().size()); ++i) {
      preset->setCurrentIndex(i);
      QCOMPARE(dialog.material().name, MaterialDialog::presets()[static_cast<size_t>(i)].name);
      QCOMPARE(dialog.material().density, MaterialDialog::presets()[static_cast<size_t>(i)].density);
    }
  }

  void typingMakesACustomMaterial() {
    MaterialDialog dialog;
    auto* preset = dialog.findChild<QComboBox*>(QStringLiteral("materialPreset"));
    auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("materialName"));
    auto* density = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("materialDensity"));
    QVERIFY(preset && name && density);
    density->setValue(7900.0);
    QCOMPARE(preset->currentIndex(), preset->count() - 1); // Custom
    name->clear();
    QTest::keyClicks(name, QStringLiteral("  Tool steel  "));
    QCOMPARE(dialog.material().name, std::string("Tool steel")); // trimmed
    QCOMPARE(dialog.material().density, 7900.0);
  }

  void theCurrentMaterialIsPreselected() {
    MaterialDialog aluminium(sc::core::Material{"Aluminium", 2700.0});
    QCOMPARE(aluminium.findChild<QComboBox*>(QStringLiteral("materialPreset"))->currentText().left(9),
             QStringLiteral("Aluminium"));
    MaterialDialog custom(sc::core::Material{"Inconel 718", 8190.0});
    auto* preset = custom.findChild<QComboBox*>(QStringLiteral("materialPreset"));
    QCOMPARE(preset->currentIndex(), preset->count() - 1);
    QCOMPARE(custom.material().name, std::string("Inconel 718"));
    QCOMPARE(custom.material().density, 8190.0);
  }
};

int runMaterialDialogTests(int argc, char** argv) {
  MaterialDialogTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_material_dialog.moc"
