#include "app/MaterialDialog.h"

#include "core/Units.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>

namespace sc::app {

namespace {

// The combo's last entry: anything not on the list.
int customIndex(const QComboBox* combo) {
  return combo->count() - 1;
}

} // namespace

const std::vector<core::Material>& MaterialDialog::presets() {
  // Typical room-temperature densities (kg/m³); alloys and grades vary by a few percent.
  static const std::vector<core::Material> List = {
      {"Steel", 7850.0}, {"Stainless steel", 8000.0}, {"Aluminium", 2700.0},     {"Titanium", 4430.0},
      {"Brass", 8500.0}, {"Copper", 8960.0},          {"Cast iron", 7200.0},     {"ABS", 1050.0},
      {"PLA", 1240.0},   {"Nylon (PA6)", 1140.0},     {"Polycarbonate", 1200.0}, {"Oak", 750.0},
  };
  return List;
}

MaterialDialog::MaterialDialog(const std::optional<core::Material>& current, QWidget* parent)
    : QDialog(parent) {
  setWindowTitle(tr("Set Material"));

  _preset = new QComboBox(this);
  _preset->setObjectName(QStringLiteral("materialPreset"));
  for (const core::Material& material : presets()) {
    _preset->addItem(
        tr("%1 (%2 kg/m³)").arg(QString::fromStdString(material.name)).arg(material.density, 0, 'f', 0));
  }
  _preset->addItem(tr("Custom"));

  _name = new QLineEdit(this);
  _name->setObjectName(QStringLiteral("materialName"));
  _density = new QDoubleSpinBox(this);
  _density->setObjectName(QStringLiteral("materialDensity"));
  _density->setRange(1.0, 30000.0);
  _density->setDecimals(1);
  _density->setSingleStep(10.0);
  _density->setSuffix(QStringLiteral(" kg/m³"));
  _hint = new QLabel(this);

  auto* form = new QFormLayout;
  form->addRow(tr("Material:"), _preset);
  form->addRow(tr("Name:"), _name);
  form->addRow(tr("Density:"), _density);
  form->addRow(QString(), _hint);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(_name, &QLineEdit::textChanged, this, [buttons](const QString& text) {
    buttons->button(QDialogButtonBox::Ok)->setEnabled(!text.trimmed().isEmpty());
  });

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);

  connect(_preset, &QComboBox::currentIndexChanged, this, &MaterialDialog::onPresetChosen);
  connect(_name, &QLineEdit::textEdited, this, &MaterialDialog::onEdited);
  connect(_density, &QDoubleSpinBox::valueChanged, this, [this](double) {
    onEdited();
    updateHint();
  });

  // Start on the current material: its preset when it is one, else Custom with its values.
  int start = 0;
  if (current) {
    start = customIndex(_preset);
    for (int i = 0; i < static_cast<int>(presets().size()); ++i) {
      const core::Material& preset = presets()[static_cast<size_t>(i)];
      if (preset.name == current->name && std::abs(preset.density - current->density) < 1e-6) {
        start = i;
      }
    }
  }
  _preset->setCurrentIndex(start);
  onPresetChosen(start);
  if (current && start == customIndex(_preset)) {
    _filling = true;
    _name->setText(QString::fromStdString(current->name));
    _density->setValue(current->density);
    _filling = false;
  }
  updateHint();
}

void MaterialDialog::onPresetChosen(int index) {
  if (index < 0 || index >= static_cast<int>(presets().size())) {
    return; // Custom keeps whatever is typed
  }
  _filling = true;
  _name->setText(QString::fromStdString(presets()[static_cast<size_t>(index)].name));
  _density->setValue(presets()[static_cast<size_t>(index)].density);
  _filling = false;
  updateHint();
}

void MaterialDialog::onEdited() {
  if (_filling) {
    return;
  }
  // Typing over a preset's values makes it a custom material.
  const QSignalBlocker blocker(_preset);
  _preset->setCurrentIndex(customIndex(_preset));
}

void MaterialDialog::updateHint() {
  _hint->setText(tr("= %1 g/cm³").arg(_density->value() / 1000.0, 0, 'f', 3));
}

core::Material MaterialDialog::material() const {
  return core::Material{_name->text().trimmed().toStdString(), _density->value()};
}

} // namespace sc::app
