#include "app/PartsListDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QVBoxLayout>

namespace sc::app {

namespace {

constexpr const char* PartsKey = "partsList/parts";
constexpr const char* VolumeKey = "partsList/volume";
constexpr const char* MassKey = "partsList/mass";
constexpr const char* BoxKey = "partsList/boundingBox";

} // namespace

PartsListDialog::PartsListDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Export Parts List"));

  _all = new QRadioButton(tr("All parts"), this);
  _all->setObjectName(QStringLiteral("partsListAll"));
  _visible = new QRadioButton(tr("Visible parts only"), this);
  _visible->setObjectName(QStringLiteral("partsListVisible"));
  auto* partsBox = new QGroupBox(tr("Parts"), this);
  auto* partsLayout = new QVBoxLayout(partsBox);
  partsLayout->addWidget(_all);
  partsLayout->addWidget(_visible);

  _volume = new QCheckBox(tr("Volume"), this);
  _volume->setObjectName(QStringLiteral("partsListVolume"));
  _mass = new QCheckBox(tr("Material and mass"), this);
  _mass->setObjectName(QStringLiteral("partsListMass"));
  _mass->setToolTip(tr("From each part's material (Measure > Set Material, or the file's)"));
  _boundingBox = new QCheckBox(tr("Size (bounding box)"), this);
  _boundingBox->setObjectName(QStringLiteral("partsListBoundingBox"));
  auto* columnsBox = new QGroupBox(tr("Columns"), this);
  auto* columnsLayout = new QVBoxLayout(columnsBox);
  columnsLayout->addWidget(_volume);
  columnsLayout->addWidget(_mass);
  columnsLayout->addWidget(_boundingBox);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Export..."));
  connect(buttons, &QDialogButtonBox::accepted, this, [this] {
    saveSettings();
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(partsBox);
  layout->addWidget(columnsBox);
  layout->addWidget(buttons);

  const QSettings settings;
  (settings.value(QLatin1String(PartsKey)).toString() == QLatin1String("visible") ? _visible : _all)
      ->setChecked(true);
  _volume->setChecked(settings.value(QLatin1String(VolumeKey), true).toBool());
  _mass->setChecked(settings.value(QLatin1String(MassKey), true).toBool());
  _boundingBox->setChecked(settings.value(QLatin1String(BoxKey), true).toBool());
}

bool PartsListDialog::visibleOnly() const {
  return _visible->isChecked();
}

bool PartsListDialog::volume() const {
  return _volume->isChecked();
}

bool PartsListDialog::mass() const {
  return _mass->isChecked();
}

bool PartsListDialog::boundingBox() const {
  return _boundingBox->isChecked();
}

void PartsListDialog::saveSettings() const {
  QSettings settings;
  settings.setValue(QLatin1String(PartsKey),
                    visibleOnly() ? QStringLiteral("visible") : QStringLiteral("all"));
  settings.setValue(QLatin1String(VolumeKey), volume());
  settings.setValue(QLatin1String(MassKey), mass());
  settings.setValue(QLatin1String(BoxKey), boundingBox());
}

} // namespace sc::app
