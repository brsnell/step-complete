#include "app/ThicknessPanel.h"

#include "core/Units.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace sc::app {

namespace {

QString qs(const std::string& text) {
  return QString::fromStdString(text);
}

QString idleText() {
  return ThicknessPanel::tr("Measure > Wall Thickness colors the selected parts, or every visible part, by "
                            "their wall thickness.");
}

QDoubleSpinBox* limitBox(const QString& name, QWidget* parent) {
  auto* box = new QDoubleSpinBox(parent);
  box->setObjectName(name);
  box->setRange(0.0, 1e6);
  box->setKeyboardTracking(false);
  return box;
}

} // namespace

ThicknessPanel::ThicknessPanel(QWidget* parent) : QWidget(parent) {
  _summary = new QLabel(idleText(), this);
  _summary->setObjectName(QStringLiteral("thicknessSummary"));
  _summary->setWordWrap(true);
  _summary->setContentsMargins(4, 2, 4, 2);

  _progress = new QProgressBar(this);
  _progress->setRange(0, 100);
  _progress->setVisible(false);
  _cancel = new QToolButton(this);
  _cancel->setText(tr("Cancel"));
  _cancel->setAutoRaise(true);
  _cancel->setVisible(false);
  auto* bar = new QHBoxLayout;
  bar->setContentsMargins(0, 0, 0, 0);
  bar->addWidget(_progress, 1);
  bar->addWidget(_cancel);

  _limits = new QWidget(this);
  _thin = limitBox(QStringLiteral("thicknessThin"), _limits);
  _thin->setToolTip(tr("Walls this thin or thinner are red"));
  _thick = limitBox(QStringLiteral("thicknessThick"), _limits);
  _thick->setToolTip(tr("Walls this thick or thicker are blue"));
  _reset = new QToolButton(_limits);
  _reset->setText(tr("Reset"));
  _reset->setToolTip(tr("Back to the range found"));
  _reset->setAutoRaise(true);
  auto* form = new QFormLayout(_limits);
  form->setContentsMargins(4, 0, 4, 0);
  form->addRow(tr("Thin limit (red):"), _thin);
  form->addRow(tr("Thick limit (blue):"), _thick);
  form->addRow(QString(), _reset);
  _limits->setEnabled(false);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(2, 2, 2, 2);
  layout->addWidget(_summary);
  layout->addLayout(bar);
  layout->addWidget(_limits);
  layout->addStretch();

  connect(_cancel, &QToolButton::clicked, this, &ThicknessPanel::cancelRequested);
  connect(_thin, &QDoubleSpinBox::valueChanged, this, &ThicknessPanel::emitLimits);
  connect(_thick, &QDoubleSpinBox::valueChanged, this, &ThicknessPanel::emitLimits);
  connect(_reset, &QToolButton::clicked, this, [this] {
    setLimits(_foundMin, _foundMax);
    emitLimits();
  });
}

QString ThicknessPanel::summaryOf(const core::ThicknessResult& result,
                                  const core::MeasureDisplayOptions& units) {
  if (result.cancelled) {
    return tr("Wall thickness cancelled.");
  }
  const int measured = result.definitions - result.withoutValue;
  if (measured == 0) {
    return tr("No wall thickness found: the parts are sheets or have no mesh.");
  }
  QString text =
      tr("Wall thickness %1 to %2 in %3.")
          .arg(qs(core::formatNumber(result.min / core::millimetersPer(units.lengthUnit), units.decimals)),
               qs(core::formatLength(result.max, units.lengthUnit, units.decimals)),
               measured == 1 ? tr("1 part") : tr("%1 parts").arg(measured));
  if (result.withoutValue > 0) {
    text += QLatin1Char(' ') +
            (result.withoutValue == 1
                 ? tr("1 part has no wall (a sheet) and is not colored.")
                 : tr("%1 parts have no wall (sheets) and are not colored.").arg(result.withoutValue));
  }
  if (result.points > 0 && result.valued < result.points) {
    const double share =
        100.0 * static_cast<double>(result.points - result.valued) / static_cast<double>(result.points);
    if (share >= 0.1) {
      text +=
          QLatin1Char(' ') + tr("%1% of the surface found no wall behind it (grey).").arg(share, 0, 'f', 1);
    }
  }
  return text;
}

void ThicknessPanel::setRunning(int percent, const QString& stage) {
  _running = true;
  _summary->setText(stage.isEmpty() ? tr("Measuring wall thickness…") : stage + QStringLiteral("…"));
  _progress->setValue(percent);
  _progress->setVisible(true);
  _cancel->setVisible(true);
  _limits->setEnabled(false);
}

void ThicknessPanel::setResult(const core::ThicknessResult& result,
                               const core::MeasureDisplayOptions& units) {
  _running = false;
  _progress->setVisible(false);
  _cancel->setVisible(false);
  _summary->setText(summaryOf(result, units));
  _perUnit = core::millimetersPer(units.lengthUnit);
  const QString suffix = QLatin1Char(' ') + QString::fromUtf8(core::symbol(units.lengthUnit).data());
  for (QDoubleSpinBox* box : {_thin, _thick}) {
    const QSignalBlocker blocker(box);
    box->setDecimals(units.decimals);
    box->setSingleStep(std::pow(10.0, -std::min(units.decimals, 2)));
    box->setSuffix(suffix);
  }
  _foundMin = result.min;
  _foundMax = result.max;
  setLimits(result.min, result.max);
  _limits->setEnabled(result.definitions > result.withoutValue && !result.cancelled);
}

void ThicknessPanel::clear() {
  _running = false;
  _progress->setVisible(false);
  _cancel->setVisible(false);
  _limits->setEnabled(false);
  _summary->setText(idleText());
}

QString ThicknessPanel::summary() const {
  return _summary->text();
}

double ThicknessPanel::thinLimit() const {
  return _thin->value() * _perUnit;
}

double ThicknessPanel::thickLimit() const {
  return _thick->value() * _perUnit;
}

void ThicknessPanel::setLimits(double thin, double thick) {
  _updating = true;
  _thin->setValue(thin / _perUnit);
  _thick->setValue(thick / _perUnit);
  _updating = false;
}

void ThicknessPanel::emitLimits() {
  if (!_updating) {
    Q_EMIT limitsChanged(thinLimit(), thickLimit());
  }
}

} // namespace sc::app
