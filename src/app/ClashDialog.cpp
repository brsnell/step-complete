#include "app/ClashDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace sc::app {

namespace {

constexpr const char* TouchingKey = "clash/touching";
constexpr const char* ClearanceKey = "clash/clearance";
constexpr const char* ClearanceValueKey = "clash/clearanceMm";

} // namespace

ClashDialog::ClashDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Check Clashes"));

  auto* intro = new QLabel(
      tr("Checks the selected parts against each other, or every visible part when nothing is selected. "
         "Parts that overlap are always reported."),
      this);
  intro->setWordWrap(true);

  _touching = new QCheckBox(tr("Also report parts that touch"), this);
  _touching->setObjectName(QStringLiteral("clashTouching"));
  _touching->setToolTip(tr("Faces in contact without overlapping, as seated parts are"));
  _clearance = new QCheckBox(tr("Also report gaps smaller than"), this);
  _clearance->setObjectName(QStringLiteral("clashClearance"));
  _clearanceValue = new QDoubleSpinBox(this);
  _clearanceValue->setObjectName(QStringLiteral("clashClearanceValue"));
  _clearanceValue->setRange(0.001, 1000.0);
  _clearanceValue->setDecimals(3);
  _clearanceValue->setSuffix(QStringLiteral(" mm"));
  connect(_clearance, &QCheckBox::toggled, _clearanceValue, &QWidget::setEnabled);

  auto* clearanceRow = new QHBoxLayout;
  clearanceRow->addWidget(_clearance);
  clearanceRow->addWidget(_clearanceValue);
  clearanceRow->addStretch();
  auto* reportBox = new QGroupBox(tr("Report"), this);
  auto* reportLayout = new QVBoxLayout(reportBox);
  reportLayout->addWidget(_touching);
  reportLayout->addLayout(clearanceRow);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Check"));
  connect(buttons, &QDialogButtonBox::accepted, this, [this] {
    saveSettings();
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(intro);
  layout->addWidget(reportBox);
  layout->addWidget(buttons);

  const QSettings settings;
  _touching->setChecked(settings.value(QLatin1String(TouchingKey), false).toBool());
  _clearance->setChecked(settings.value(QLatin1String(ClearanceKey), false).toBool());
  _clearanceValue->setValue(settings.value(QLatin1String(ClearanceValueKey), 1.0).toDouble());
  _clearanceValue->setEnabled(_clearance->isChecked());
}

core::ClashOptions ClashDialog::options() const {
  core::ClashOptions options;
  options.touching = _touching->isChecked();
  if (_clearance->isChecked()) {
    options.clearance = _clearanceValue->value();
  }
  return options;
}

void ClashDialog::saveSettings() const {
  QSettings settings;
  settings.setValue(QLatin1String(TouchingKey), _touching->isChecked());
  settings.setValue(QLatin1String(ClearanceKey), _clearance->isChecked());
  settings.setValue(QLatin1String(ClearanceValueKey), _clearanceValue->value());
}

} // namespace sc::app
