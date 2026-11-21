#include "app/ExportImageDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace sc::app {

ExportImageDialog::ExportImageDialog(const QSize& viewSize, int maxSize, bool allowTransparent,
                                     QWidget* parent)
    : QDialog(parent), _viewSize(viewSize.expandedTo(QSize(1, 1))) {
  setWindowTitle(tr("Export Image"));

  _currentSize = new QRadioButton(
      tr("Current view size (%1 x %2 px)").arg(_viewSize.width()).arg(_viewSize.height()), this);
  _customSize = new QRadioButton(tr("Custom size:"), this);
  _currentSize->setChecked(true);

  _width = new QSpinBox(this);
  _height = new QSpinBox(this);
  for (QSpinBox* spin : {_width, _height}) {
    spin->setRange(1, maxSize);
    spin->setSuffix(tr(" px"));
  }
  _width->setValue(std::min(_viewSize.width(), maxSize));
  _height->setValue(std::min(_viewSize.height(), maxSize));
  _keepAspect = new QCheckBox(tr("Keep aspect ratio"), this);
  _keepAspect->setChecked(true);

  _background = new QComboBox(this);
  _background->addItem(tr("As shown"), int(render::ExportBackground::AsShown));
  _background->addItem(tr("White"), int(render::ExportBackground::White));
  if (allowTransparent) {
    _background->addItem(tr("Transparent"), int(render::ExportBackground::Transparent));
  }

  auto* sizeForm = new QFormLayout;
  sizeForm->addRow(tr("Width:"), _width);
  sizeForm->addRow(tr("Height:"), _height);
  sizeForm->addRow(QString(), _keepAspect);
  sizeForm->setContentsMargins(24, 0, 0, 0);

  auto* backgroundForm = new QFormLayout;
  backgroundForm->addRow(tr("Background:"), _background);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(_currentSize);
  layout->addWidget(_customSize);
  layout->addLayout(sizeForm);
  layout->addLayout(backgroundForm);
  layout->addWidget(buttons);

  connect(_customSize, &QRadioButton::toggled, this, &ExportImageDialog::updateEnabled);
  connect(_width, &QSpinBox::valueChanged, this, &ExportImageDialog::onWidthEdited);
  connect(_height, &QSpinBox::valueChanged, this, &ExportImageDialog::onHeightEdited);
  updateEnabled();
}

void ExportImageDialog::updateEnabled() {
  const bool custom = _customSize->isChecked();
  _width->setEnabled(custom);
  _height->setEnabled(custom);
  _keepAspect->setEnabled(custom);
}

void ExportImageDialog::onWidthEdited(int width) {
  if (!_keepAspect->isChecked()) {
    return;
  }
  const double aspect = double(_viewSize.height()) / _viewSize.width();
  const QSignalBlocker blocker(_height);
  _height->setValue(std::clamp(int(std::lround(width * aspect)), 1, _height->maximum()));
}

void ExportImageDialog::onHeightEdited(int height) {
  if (!_keepAspect->isChecked()) {
    return;
  }
  const double aspect = double(_viewSize.width()) / _viewSize.height();
  const QSignalBlocker blocker(_width);
  _width->setValue(std::clamp(int(std::lround(height * aspect)), 1, _width->maximum()));
}

QSize ExportImageDialog::imageSize() const {
  return _customSize->isChecked() ? QSize(_width->value(), _height->value()) : _viewSize;
}

render::ExportBackground ExportImageDialog::background() const {
  return static_cast<render::ExportBackground>(_background->currentData().toInt());
}

} // namespace sc::app
