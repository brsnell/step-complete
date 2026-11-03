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
    : QDialog(parent), m_viewSize(viewSize.expandedTo(QSize(1, 1))) {
  setWindowTitle(tr("Export Image"));

  m_currentSize = new QRadioButton(
      tr("Current view size (%1 x %2 px)").arg(m_viewSize.width()).arg(m_viewSize.height()), this);
  m_customSize = new QRadioButton(tr("Custom size:"), this);
  m_currentSize->setChecked(true);

  m_width = new QSpinBox(this);
  m_height = new QSpinBox(this);
  for (QSpinBox* spin : {m_width, m_height}) {
    spin->setRange(1, maxSize);
    spin->setSuffix(tr(" px"));
  }
  m_width->setValue(std::min(m_viewSize.width(), maxSize));
  m_height->setValue(std::min(m_viewSize.height(), maxSize));
  m_keepAspect = new QCheckBox(tr("Keep aspect ratio"), this);
  m_keepAspect->setChecked(true);

  m_background = new QComboBox(this);
  m_background->addItem(tr("As shown"), int(render::ExportBackground::AsShown));
  m_background->addItem(tr("White"), int(render::ExportBackground::White));
  if (allowTransparent) {
    m_background->addItem(tr("Transparent"), int(render::ExportBackground::Transparent));
  }

  auto* sizeForm = new QFormLayout;
  sizeForm->addRow(tr("Width:"), m_width);
  sizeForm->addRow(tr("Height:"), m_height);
  sizeForm->addRow(QString(), m_keepAspect);
  sizeForm->setContentsMargins(24, 0, 0, 0);

  auto* backgroundForm = new QFormLayout;
  backgroundForm->addRow(tr("Background:"), m_background);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(m_currentSize);
  layout->addWidget(m_customSize);
  layout->addLayout(sizeForm);
  layout->addLayout(backgroundForm);
  layout->addWidget(buttons);

  connect(m_customSize, &QRadioButton::toggled, this, &ExportImageDialog::updateEnabled);
  connect(m_width, &QSpinBox::valueChanged, this, &ExportImageDialog::onWidthEdited);
  connect(m_height, &QSpinBox::valueChanged, this, &ExportImageDialog::onHeightEdited);
  updateEnabled();
}

void ExportImageDialog::updateEnabled() {
  const bool custom = m_customSize->isChecked();
  m_width->setEnabled(custom);
  m_height->setEnabled(custom);
  m_keepAspect->setEnabled(custom);
}

void ExportImageDialog::onWidthEdited(int width) {
  if (!m_keepAspect->isChecked()) {
    return;
  }
  const double aspect = double(m_viewSize.height()) / m_viewSize.width();
  const QSignalBlocker blocker(m_height);
  m_height->setValue(std::clamp(int(std::lround(width * aspect)), 1, m_height->maximum()));
}

void ExportImageDialog::onHeightEdited(int height) {
  if (!m_keepAspect->isChecked()) {
    return;
  }
  const double aspect = double(m_viewSize.width()) / m_viewSize.height();
  const QSignalBlocker blocker(m_width);
  m_width->setValue(std::clamp(int(std::lround(height * aspect)), 1, m_width->maximum()));
}

QSize ExportImageDialog::imageSize() const {
  return m_customSize->isChecked() ? QSize(m_width->value(), m_height->value()) : m_viewSize;
}

render::ExportBackground ExportImageDialog::background() const {
  return static_cast<render::ExportBackground>(m_background->currentData().toInt());
}

} // namespace sc::app
