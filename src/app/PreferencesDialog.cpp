#include "app/PreferencesDialog.h"

#include "app/ImportJob.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace sc::app {

namespace {

constexpr const char* StyleKey = "view/backgroundStyle";
constexpr const char* TopKey = "view/backgroundTop";
constexpr const char* BottomKey = "view/backgroundBottom";
constexpr const char* SolidKey = "view/backgroundSolid";
constexpr const char* LightingKey = "view/lighting";
constexpr const char* AntialiasingKey = "view/antialiasing";

QColor readColor(const QSettings& settings, const char* key, const QColor& fallback) {
  const QColor color(settings.value(QLatin1String(key)).toString());
  return color.isValid() ? color : fallback;
}

void setSwatch(QPushButton* button, const QColor& color) {
  QPixmap swatch(28, 14);
  swatch.fill(color);
  button->setIcon(QIcon(swatch));
  button->setText(color.name().toUpper());
}

} // namespace

ViewPreferences ViewPreferences::load() {
  ViewPreferences prefs; // defaults
  const QSettings settings;
  const int style = settings.value(QLatin1String(StyleKey), 0).toInt();
  if (style >= 0 && style <= int(render::BackgroundStyle::Kind::White)) {
    prefs.background.kind = static_cast<render::BackgroundStyle::Kind>(style);
  }
  prefs.background.top = readColor(settings, TopKey, prefs.background.top);
  prefs.background.bottom = readColor(settings, BottomKey, prefs.background.bottom);
  prefs.background.solid = readColor(settings, SolidKey, prefs.background.solid);
  const int lighting = settings.value(QLatin1String(LightingKey), 0).toInt();
  if (lighting >= 0 && lighting <= int(render::LightingPreset::Softer)) {
    prefs.lighting = static_cast<render::LightingPreset>(lighting);
  }
  prefs.antialiasing = settings.value(QLatin1String(AntialiasingKey), false).toBool();
  return prefs;
}

void ViewPreferences::save() const {
  QSettings settings;
  settings.setValue(QLatin1String(StyleKey), int(background.kind));
  settings.setValue(QLatin1String(TopKey), background.top.name());
  settings.setValue(QLatin1String(BottomKey), background.bottom.name());
  settings.setValue(QLatin1String(SolidKey), background.solid.name());
  settings.setValue(QLatin1String(LightingKey), int(lighting));
  settings.setValue(QLatin1String(AntialiasingKey), antialiasing);
}

PreferencesDialog::PreferencesDialog(const ViewPreferences& initial, QWidget* parent)
    : QDialog(parent), _initial(initial), _current(initial) {
  setWindowTitle(tr("Preferences"));

  _styleCombo = new QComboBox(this);
  _styleCombo->setObjectName(QStringLiteral("backgroundStyle"));
  _styleCombo->addItem(tr("Gradient"), int(render::BackgroundStyle::Kind::Gradient));
  _styleCombo->addItem(tr("Solid color"), int(render::BackgroundStyle::Kind::Solid));
  _styleCombo->addItem(tr("White"), int(render::BackgroundStyle::Kind::White));

  auto makeButton = [this](const char* name) {
    auto* button = new QPushButton(this);
    button->setObjectName(QLatin1String(name));
    return button;
  };
  _topButton = makeButton("gradientTop");
  _bottomButton = makeButton("gradientBottom");
  _solidButton = makeButton("solidColor");
  connect(_topButton, &QPushButton::clicked, this,
          [this] { chooseColor(_current.background.top, tr("Gradient top color")); });
  connect(_bottomButton, &QPushButton::clicked, this,
          [this] { chooseColor(_current.background.bottom, tr("Gradient bottom color")); });
  connect(_solidButton, &QPushButton::clicked, this,
          [this] { chooseColor(_current.background.solid, tr("Background color")); });

  _lightingCombo = new QComboBox(this);
  _lightingCombo->setObjectName(QStringLiteral("lighting"));
  _lightingCombo->addItem(tr("Default"), int(render::LightingPreset::Default));
  _lightingCombo->addItem(tr("Brighter"), int(render::LightingPreset::Brighter));
  _lightingCombo->addItem(tr("Softer"), int(render::LightingPreset::Softer));

  _antialiasing = new QCheckBox(tr("Smooth edges (4x MSAA; slower on old graphics hardware)"), this);
  _antialiasing->setObjectName(QStringLiteral("antialiasing"));

  auto* form = new QFormLayout;
  form->addRow(tr("Background:"), _styleCombo);
  form->addRow(tr("Top color:"), _topButton);
  form->addRow(tr("Bottom color:"), _bottomButton);
  form->addRow(tr("Color:"), _solidButton);
  form->addRow(tr("Lighting:"), _lightingCombo);
  form->addRow(tr("Antialiasing:"), _antialiasing);

  // Open-file cache: applied on OK (it only affects the next file opened).
  _cacheEnabled = new QCheckBox(tr("Keep a cache of opened files so they reopen quickly"), this);
  _cacheEnabled->setObjectName(QStringLiteral("cacheEnabled"));
  _cacheEnabled->setChecked(ImportJob::cacheEnabled());
  _cacheSize = new QLabel(this);
  _cacheSize->setObjectName(QStringLiteral("cacheSize"));
  _clearCache = new QPushButton(tr("Clear Cache"), this);
  _clearCache->setObjectName(QStringLiteral("clearCache"));
  connect(_clearCache, &QPushButton::clicked, this, [this] {
    const QString dir = ImportJob::documentCacheDir();
    if (!dir.isEmpty()) {
      QDir(dir).removeRecursively();
    }
    updateCacheSize();
  });
  auto* cacheRow = new QHBoxLayout;
  cacheRow->addWidget(_cacheSize, 1);
  cacheRow->addWidget(_clearCache);
  auto* cacheBox = new QGroupBox(tr("Cache"), this);
  auto* cacheLayout = new QVBoxLayout(cacheBox);
  cacheLayout->addWidget(_cacheEnabled);
  cacheLayout->addLayout(cacheRow);
  updateCacheSize();

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(cacheBox);
  layout->addWidget(buttons);

  connect(_styleCombo, &QComboBox::currentIndexChanged, this, [this] {
    _current.background.kind = static_cast<render::BackgroundStyle::Kind>(_styleCombo->currentData().toInt());
    edited();
  });
  connect(_lightingCombo, &QComboBox::currentIndexChanged, this, [this] {
    _current.lighting = static_cast<render::LightingPreset>(_lightingCombo->currentData().toInt());
    edited();
  });
  connect(_antialiasing, &QCheckBox::toggled, this, [this](bool on) {
    _current.antialiasing = on;
    edited();
  });

  updateWidgets();
}

void PreferencesDialog::chooseColor(QColor& target, const QString& title) {
  const QColor color = QColorDialog::getColor(target, this, title);
  if (color.isValid() && color != target) {
    target = color;
    edited();
  }
}

void PreferencesDialog::updateWidgets() {
  _styleCombo->blockSignals(true);
  _lightingCombo->blockSignals(true);
  _antialiasing->blockSignals(true);
  _styleCombo->setCurrentIndex(_styleCombo->findData(int(_current.background.kind)));
  _lightingCombo->setCurrentIndex(_lightingCombo->findData(int(_current.lighting)));
  _antialiasing->setChecked(_current.antialiasing);
  _styleCombo->blockSignals(false);
  _lightingCombo->blockSignals(false);
  _antialiasing->blockSignals(false);

  setSwatch(_topButton, _current.background.top);
  setSwatch(_bottomButton, _current.background.bottom);
  setSwatch(_solidButton, _current.background.solid);
  const bool gradient = _current.background.kind == render::BackgroundStyle::Kind::Gradient;
  const bool solid = _current.background.kind == render::BackgroundStyle::Kind::Solid;
  _topButton->setEnabled(gradient);
  _bottomButton->setEnabled(gradient);
  _solidButton->setEnabled(solid);
}

void PreferencesDialog::edited() {
  updateWidgets();
  Q_EMIT preferencesChanged(_current);
}

void PreferencesDialog::setPreferences(const ViewPreferences& preferences) {
  if (preferences == _current) {
    return;
  }
  _current = preferences;
  edited();
}

void PreferencesDialog::updateCacheSize() {
  qint64 bytes = 0;
  const QString dir = ImportJob::documentCacheDir();
  if (!dir.isEmpty()) {
    QDirIterator it(dir, QDir::Files);
    while (it.hasNext()) {
      it.next();
      bytes += it.fileInfo().size();
    }
  }
  _cacheSize->setText(tr("Cache size: %1").arg(QLocale().formattedDataSize(bytes)));
  _clearCache->setEnabled(bytes > 0);
}

void PreferencesDialog::accept() {
  ImportJob::setCacheEnabled(_cacheEnabled->isChecked());
  QDialog::accept();
}

void PreferencesDialog::reject() {
  setPreferences(_initial); // undo the live preview
  QDialog::reject();
}

} // namespace sc::app
