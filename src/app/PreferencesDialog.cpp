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

constexpr const char* kStyleKey = "view/backgroundStyle";
constexpr const char* kTopKey = "view/backgroundTop";
constexpr const char* kBottomKey = "view/backgroundBottom";
constexpr const char* kSolidKey = "view/backgroundSolid";
constexpr const char* kLightingKey = "view/lighting";
constexpr const char* kAntialiasingKey = "view/antialiasing";

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
  const int style = settings.value(QLatin1String(kStyleKey), 0).toInt();
  if (style >= 0 && style <= int(render::BackgroundStyle::Kind::White)) {
    prefs.background.kind = static_cast<render::BackgroundStyle::Kind>(style);
  }
  prefs.background.top = readColor(settings, kTopKey, prefs.background.top);
  prefs.background.bottom = readColor(settings, kBottomKey, prefs.background.bottom);
  prefs.background.solid = readColor(settings, kSolidKey, prefs.background.solid);
  const int lighting = settings.value(QLatin1String(kLightingKey), 0).toInt();
  if (lighting >= 0 && lighting <= int(render::LightingPreset::Softer)) {
    prefs.lighting = static_cast<render::LightingPreset>(lighting);
  }
  prefs.antialiasing = settings.value(QLatin1String(kAntialiasingKey), false).toBool();
  return prefs;
}

void ViewPreferences::save() const {
  QSettings settings;
  settings.setValue(QLatin1String(kStyleKey), int(background.kind));
  settings.setValue(QLatin1String(kTopKey), background.top.name());
  settings.setValue(QLatin1String(kBottomKey), background.bottom.name());
  settings.setValue(QLatin1String(kSolidKey), background.solid.name());
  settings.setValue(QLatin1String(kLightingKey), int(lighting));
  settings.setValue(QLatin1String(kAntialiasingKey), antialiasing);
}

PreferencesDialog::PreferencesDialog(const ViewPreferences& initial, QWidget* parent)
    : QDialog(parent), m_initial(initial), m_current(initial) {
  setWindowTitle(tr("Preferences"));

  m_styleCombo = new QComboBox(this);
  m_styleCombo->setObjectName(QStringLiteral("backgroundStyle"));
  m_styleCombo->addItem(tr("Gradient"), int(render::BackgroundStyle::Kind::Gradient));
  m_styleCombo->addItem(tr("Solid color"), int(render::BackgroundStyle::Kind::Solid));
  m_styleCombo->addItem(tr("White"), int(render::BackgroundStyle::Kind::White));

  auto makeButton = [this](const char* name) {
    auto* button = new QPushButton(this);
    button->setObjectName(QLatin1String(name));
    return button;
  };
  m_topButton = makeButton("gradientTop");
  m_bottomButton = makeButton("gradientBottom");
  m_solidButton = makeButton("solidColor");
  connect(m_topButton, &QPushButton::clicked, this,
          [this] { chooseColor(m_current.background.top, tr("Gradient top color")); });
  connect(m_bottomButton, &QPushButton::clicked, this,
          [this] { chooseColor(m_current.background.bottom, tr("Gradient bottom color")); });
  connect(m_solidButton, &QPushButton::clicked, this,
          [this] { chooseColor(m_current.background.solid, tr("Background color")); });

  m_lightingCombo = new QComboBox(this);
  m_lightingCombo->setObjectName(QStringLiteral("lighting"));
  m_lightingCombo->addItem(tr("Default"), int(render::LightingPreset::Default));
  m_lightingCombo->addItem(tr("Brighter"), int(render::LightingPreset::Brighter));
  m_lightingCombo->addItem(tr("Softer"), int(render::LightingPreset::Softer));

  m_antialiasing = new QCheckBox(tr("Smooth edges (4x MSAA; slower on old graphics hardware)"), this);
  m_antialiasing->setObjectName(QStringLiteral("antialiasing"));

  auto* form = new QFormLayout;
  form->addRow(tr("Background:"), m_styleCombo);
  form->addRow(tr("Top color:"), m_topButton);
  form->addRow(tr("Bottom color:"), m_bottomButton);
  form->addRow(tr("Color:"), m_solidButton);
  form->addRow(tr("Lighting:"), m_lightingCombo);
  form->addRow(tr("Antialiasing:"), m_antialiasing);

  // Open-file cache: applied on OK (it only affects the next file opened).
  m_cacheEnabled = new QCheckBox(tr("Keep a cache of opened files so they reopen quickly"), this);
  m_cacheEnabled->setObjectName(QStringLiteral("cacheEnabled"));
  m_cacheEnabled->setChecked(ImportJob::cacheEnabled());
  m_cacheSize = new QLabel(this);
  m_cacheSize->setObjectName(QStringLiteral("cacheSize"));
  m_clearCache = new QPushButton(tr("Clear Cache"), this);
  m_clearCache->setObjectName(QStringLiteral("clearCache"));
  connect(m_clearCache, &QPushButton::clicked, this, [this] {
    const QString dir = ImportJob::documentCacheDir();
    if (!dir.isEmpty()) {
      QDir(dir).removeRecursively();
    }
    updateCacheSize();
  });
  auto* cacheRow = new QHBoxLayout;
  cacheRow->addWidget(m_cacheSize, 1);
  cacheRow->addWidget(m_clearCache);
  auto* cacheBox = new QGroupBox(tr("Cache"), this);
  auto* cacheLayout = new QVBoxLayout(cacheBox);
  cacheLayout->addWidget(m_cacheEnabled);
  cacheLayout->addLayout(cacheRow);
  updateCacheSize();

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(cacheBox);
  layout->addWidget(buttons);

  connect(m_styleCombo, &QComboBox::currentIndexChanged, this, [this] {
    m_current.background.kind =
        static_cast<render::BackgroundStyle::Kind>(m_styleCombo->currentData().toInt());
    edited();
  });
  connect(m_lightingCombo, &QComboBox::currentIndexChanged, this, [this] {
    m_current.lighting = static_cast<render::LightingPreset>(m_lightingCombo->currentData().toInt());
    edited();
  });
  connect(m_antialiasing, &QCheckBox::toggled, this, [this](bool on) {
    m_current.antialiasing = on;
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
  m_styleCombo->blockSignals(true);
  m_lightingCombo->blockSignals(true);
  m_antialiasing->blockSignals(true);
  m_styleCombo->setCurrentIndex(m_styleCombo->findData(int(m_current.background.kind)));
  m_lightingCombo->setCurrentIndex(m_lightingCombo->findData(int(m_current.lighting)));
  m_antialiasing->setChecked(m_current.antialiasing);
  m_styleCombo->blockSignals(false);
  m_lightingCombo->blockSignals(false);
  m_antialiasing->blockSignals(false);

  setSwatch(m_topButton, m_current.background.top);
  setSwatch(m_bottomButton, m_current.background.bottom);
  setSwatch(m_solidButton, m_current.background.solid);
  const bool gradient = m_current.background.kind == render::BackgroundStyle::Kind::Gradient;
  const bool solid = m_current.background.kind == render::BackgroundStyle::Kind::Solid;
  m_topButton->setEnabled(gradient);
  m_bottomButton->setEnabled(gradient);
  m_solidButton->setEnabled(solid);
}

void PreferencesDialog::edited() {
  updateWidgets();
  Q_EMIT preferencesChanged(m_current);
}

void PreferencesDialog::setPreferences(const ViewPreferences& preferences) {
  if (preferences == m_current) {
    return;
  }
  m_current = preferences;
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
  m_cacheSize->setText(tr("Cache size: %1").arg(QLocale().formattedDataSize(bytes)));
  m_clearCache->setEnabled(bytes > 0);
}

void PreferencesDialog::accept() {
  ImportJob::setCacheEnabled(m_cacheEnabled->isChecked());
  QDialog::accept();
}

void PreferencesDialog::reject() {
  setPreferences(m_initial); // undo the live preview
  QDialog::reject();
}

} // namespace sc::app
