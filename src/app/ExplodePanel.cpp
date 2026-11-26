#include "app/ExplodePanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace sc::app {

ExplodePanel::ExplodePanel(QWidget* parent) : QWidget(parent) {
  auto* hint = new QLabel(tr("Parts move away from the center of their assembly; a sub-assembly moves "
                             "as a unit. Measuring and section views put them back."),
                          this);
  hint->setWordWrap(true);

  _slider = new QSlider(Qt::Horizontal, this);
  _slider->setObjectName(QStringLiteral("explodeDistance"));
  _slider->setRange(0, 100);
  _slider->setValue(DefaultDistance);
  _slider->setToolTip(tr("How far the parts move apart"));
  _value = new QLabel(this);
  _value->setObjectName(QStringLiteral("explodeDistanceValue"));
  _value->setMinimumWidth(_value->fontMetrics().horizontalAdvance(QStringLiteral("100 %")));
  auto* row = new QHBoxLayout;
  row->setContentsMargins(0, 0, 0, 0);
  row->addWidget(new QLabel(tr("Distance"), this));
  row->addWidget(_slider, 1);
  row->addWidget(_value);

  _collapse = new QToolButton(this);
  _collapse->setObjectName(QStringLiteral("explodeCollapse"));
  _collapse->setText(tr("Collapse"));
  _collapse->setToolTip(tr("Put the parts back together"));
  _collapse->setEnabled(false);
  auto* buttons = new QHBoxLayout;
  buttons->setContentsMargins(0, 0, 0, 0);
  buttons->addStretch(1);
  buttons->addWidget(_collapse);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->addWidget(hint);
  layout->addLayout(row);
  layout->addLayout(buttons);
  layout->addStretch(1);

  connect(_slider, &QSlider::valueChanged, this, [this](int percent) {
    showDistance();
    Q_EMIT distanceChanged(percent);
  });
  connect(_collapse, &QToolButton::clicked, this, &ExplodePanel::collapseRequested);
  showDistance();
}

int ExplodePanel::distance() const {
  return _slider->value();
}

void ExplodePanel::setDistance(int percent) {
  const QSignalBlocker blocker(_slider);
  _slider->setValue(percent);
  showDistance();
}

void ExplodePanel::setExploded(bool exploded) {
  _collapse->setEnabled(exploded);
}

void ExplodePanel::setAvailable(bool available) {
  _slider->setEnabled(available);
}

void ExplodePanel::showDistance() {
  _value->setText(tr("%1 %").arg(_slider->value()));
}

} // namespace sc::app
