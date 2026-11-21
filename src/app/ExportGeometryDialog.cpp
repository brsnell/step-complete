#include "app/ExportGeometryDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QVBoxLayout>

namespace sc::app {

namespace {

// QSettings keys and the values saved in them: names, not enum numbers, so reordering an enum
// never changes what a saved setting means.
constexpr const char* FormatKey = "exportGeometry/format";
constexpr const char* PartsKey = "exportGeometry/parts";
constexpr const char* QualityKey = "exportGeometry/quality";
constexpr const char* UnitKey = "exportGeometry/unit";
constexpr const char* ToleranceKey = "exportGeometry/tolerance";
constexpr const char* AngleKey = "exportGeometry/angle";
constexpr const char* UpAxisKey = "exportGeometry/objUpAxis";

// Selects the item whose data is 'value'; keeps the current item if there is none.
void selectData(QComboBox* combo, const QString& value) {
  const int index = combo->findData(value);
  if (index >= 0) {
    combo->setCurrentIndex(index);
  }
}

core::GeometryFormat formatFrom(const QString& name) {
  if (name == QLatin1String("stl-parts")) {
    return core::GeometryFormat::StlPerPart;
  }
  if (name == QLatin1String("obj")) {
    return core::GeometryFormat::Obj;
  }
  if (name == QLatin1String("gltf")) {
    return core::GeometryFormat::Gltf;
  }
  if (name == QLatin1String("glb")) {
    return core::GeometryFormat::Glb;
  }
  return core::GeometryFormat::StlMerged;
}

core::MeshQuality qualityFrom(const QString& name) {
  if (name == QLatin1String("fine")) {
    return core::MeshQuality::Fine;
  }
  if (name == QLatin1String("extra-fine")) {
    return core::MeshQuality::ExtraFine;
  }
  if (name == QLatin1String("custom")) {
    return core::MeshQuality::Custom;
  }
  return core::MeshQuality::AsDisplayed;
}

core::LengthUnit unitFrom(const QString& name) {
  if (name == QLatin1String("cm")) {
    return core::LengthUnit::Centimeter;
  }
  if (name == QLatin1String("m")) {
    return core::LengthUnit::Meter;
  }
  if (name == QLatin1String("in")) {
    return core::LengthUnit::Inch;
  }
  return core::LengthUnit::Millimeter;
}

bool isGltf(core::GeometryFormat format) {
  return format == core::GeometryFormat::Gltf || format == core::GeometryFormat::Glb;
}

} // namespace

ExportGeometryDialog::ExportGeometryDialog(bool hasSelection, QWidget* parent)
    : QDialog(parent), _hasSelection(hasSelection) {
  setWindowTitle(tr("Export Geometry"));

  _format = new QComboBox(this);
  _format->setObjectName(QStringLiteral("exportFormat"));
  _format->addItem(tr("STL, one file"), QStringLiteral("stl"));
  _format->addItem(tr("STL, one file per part"), QStringLiteral("stl-parts"));
  _format->addItem(tr("OBJ (with colors)"), QStringLiteral("obj"));
  _format->addItem(tr("glTF (.gltf + .bin)"), QStringLiteral("gltf"));
  _format->addItem(tr("GLB (binary glTF, one file)"), QStringLiteral("glb"));
  _format->setItemData(1, tr("One STL per unique part, in the part's own coordinates"), Qt::ToolTipRole);

  _visibleParts = new QRadioButton(tr("Visible parts"), this);
  _visibleParts->setObjectName(QStringLiteral("exportVisibleParts"));
  _selectedParts = new QRadioButton(tr("Selected parts"), this);
  _selectedParts->setObjectName(QStringLiteral("exportSelectedParts"));
  _visibleParts->setChecked(true);
  _selectedParts->setEnabled(hasSelection);
  auto* partsBox = new QGroupBox(tr("Parts"), this);
  auto* partsLayout = new QVBoxLayout(partsBox);
  partsLayout->addWidget(_visibleParts);
  partsLayout->addWidget(_selectedParts);

  _quality = new QComboBox(this);
  _quality->setObjectName(QStringLiteral("exportQuality"));
  _quality->addItem(tr("As displayed"), QStringLiteral("displayed"));
  _quality->addItem(tr("Fine"), QStringLiteral("fine"));
  _quality->addItem(tr("Extra fine"), QStringLiteral("extra-fine"));
  _quality->addItem(tr("Custom"), QStringLiteral("custom"));
  _tolerance = new QDoubleSpinBox(this);
  _tolerance->setObjectName(QStringLiteral("exportTolerance"));
  _tolerance->setDecimals(4);
  _tolerance->setRange(0.0001, 100.0);
  _tolerance->setSingleStep(0.01);
  _tolerance->setValue(0.01);
  _tolerance->setToolTip(tr("Largest distance between the mesh and the exact surface"));
  _angle = new QDoubleSpinBox(this);
  _angle->setObjectName(QStringLiteral("exportAngle"));
  _angle->setDecimals(1);
  _angle->setRange(0.5, 90.0);
  _angle->setValue(10.0);
  _angle->setSuffix(QStringLiteral("°"));
  _angle->setToolTip(tr("Largest angle between neighbouring triangles on a curved surface"));
  _memoryNote = new QLabel(tr("Finer meshes are built on a copy of the model, which uses more memory "
                              "while the export runs."),
                           this);
  _memoryNote->setWordWrap(true);

  _unit = new QComboBox(this);
  _unit->setObjectName(QStringLiteral("exportUnit"));
  _unit->addItem(tr("Millimetres"), QStringLiteral("mm"));
  _unit->addItem(tr("Centimetres"), QStringLiteral("cm"));
  _unit->addItem(tr("Metres"), QStringLiteral("m"));
  _unit->addItem(tr("Inches"), QStringLiteral("in"));

  _upAxis = new QComboBox(this);
  _upAxis->setObjectName(QStringLiteral("exportUpAxis"));
  _upAxis->addItem(tr("Y up (Blender, game engines)"), QStringLiteral("y"));
  _upAxis->addItem(tr("Z up (as in the STEP file)"), QStringLiteral("z"));

  auto* form = new QFormLayout;
  form->addRow(tr("Format:"), _format);
  form->addRow(tr("Mesh quality:"), _quality);
  form->addRow(tr("Tolerance:"), _tolerance);
  form->addRow(tr("Angle:"), _angle);
  form->addRow(QString(), _memoryNote);
  form->addRow(tr("Unit:"), _unit);
  form->addRow(tr("Up axis:"), _upAxis);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Export..."));
  connect(buttons, &QDialogButtonBox::accepted, this, [this] {
    saveSettings();
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(partsBox);
  layout->addWidget(buttons);

  loadSettings();
  connect(_format, &QComboBox::currentIndexChanged, this, &ExportGeometryDialog::updateEnabled);
  connect(_quality, &QComboBox::currentIndexChanged, this, &ExportGeometryDialog::updateEnabled);
  connect(_unit, &QComboBox::currentIndexChanged, this, &ExportGeometryDialog::updateEnabled);
  updateEnabled();
}

void ExportGeometryDialog::loadSettings() {
  const QSettings settings;
  selectData(_format, settings.value(QLatin1String(FormatKey)).toString());
  selectData(_quality, settings.value(QLatin1String(QualityKey)).toString());
  selectData(_unit, settings.value(QLatin1String(UnitKey)).toString());
  selectData(_upAxis, settings.value(QLatin1String(UpAxisKey)).toString());
  _tolerance->setValue(settings.value(QLatin1String(ToleranceKey), _tolerance->value()).toDouble());
  _angle->setValue(settings.value(QLatin1String(AngleKey), _angle->value()).toDouble());
  const bool selected = settings.value(QLatin1String(PartsKey)).toString() == QLatin1String("selected");
  (selected && _hasSelection ? _selectedParts : _visibleParts)->setChecked(true);
}

void ExportGeometryDialog::saveSettings() const {
  QSettings settings;
  settings.setValue(QLatin1String(FormatKey), _format->currentData());
  settings.setValue(QLatin1String(QualityKey), _quality->currentData());
  settings.setValue(QLatin1String(UnitKey), _unit->currentData());
  settings.setValue(QLatin1String(UpAxisKey), _upAxis->currentData());
  settings.setValue(QLatin1String(ToleranceKey), _tolerance->value());
  settings.setValue(QLatin1String(AngleKey), _angle->value());
  // A choice made without a selection says nothing about the next export: keep the old one.
  if (_hasSelection) {
    settings.setValue(QLatin1String(PartsKey),
                      _selectedParts->isChecked() ? QStringLiteral("selected") : QStringLiteral("visible"));
  }
}

void ExportGeometryDialog::updateEnabled() {
  const core::GeometryFormat format = formatFrom(_format->currentData().toString());
  const core::MeshQuality quality = qualityFrom(_quality->currentData().toString());
  const bool custom = quality == core::MeshQuality::Custom;
  _tolerance->setEnabled(custom);
  _angle->setEnabled(custom);
  _memoryNote->setVisible(quality != core::MeshQuality::AsDisplayed);
  _unit->setEnabled(!isGltf(format));
  _unit->setToolTip(isGltf(format) ? tr("glTF files are always in metres") : QString());
  _upAxis->setEnabled(format == core::GeometryFormat::Obj);
  _upAxis->setToolTip(format == core::GeometryFormat::Obj ? QString()
                      : isGltf(format)                    ? tr("glTF files are always Y up")
                                       : tr("STL files keep the model's Z up, as slicers expect"));
  // The tolerance is in the export unit; glTF's tolerance is in millimetres.
  const core::LengthUnit unit = options().unit;
  _tolerance->setSuffix(
      QLatin1Char(' ') +
      QString::fromUtf8(core::symbol(unit).data(), static_cast<qsizetype>(core::symbol(unit).size())));
}

core::GeometryExportOptions ExportGeometryDialog::options() const {
  core::GeometryExportOptions options;
  options.format = formatFrom(_format->currentData().toString());
  options.quality = qualityFrom(_quality->currentData().toString());
  options.unit =
      isGltf(options.format) ? core::LengthUnit::Millimeter : unitFrom(_unit->currentData().toString());
  options.customTolerance = _tolerance->value();
  options.customAngleDegrees = _angle->value();
  options.objUpAxis =
      _upAxis->currentData().toString() == QLatin1String("z") ? core::UpAxis::Z : core::UpAxis::Y;
  return options;
}

bool ExportGeometryDialog::selectedPartsOnly() const {
  return _selectedParts->isChecked() && _selectedParts->isEnabled();
}

} // namespace sc::app
