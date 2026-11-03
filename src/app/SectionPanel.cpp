#include "app/SectionPanel.h"

#include "app/SectionModel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace sc::app {

namespace {

constexpr int kCustomItem = 3; // combo index of "Custom" when present

// Axis index (0..2) when the normal is parallel to a coordinate axis, else -1.
int axisOf(const gp_Dir& n) {
  const double c[3] = {n.X(), n.Y(), n.Z()};
  for (int i = 0; i < 3; ++i) {
    if (std::abs(std::abs(c[i]) - 1.0) < 1e-9) {
      return i;
    }
  }
  return -1;
}

} // namespace

SectionPanel::SectionPanel(render::SectionTool* tool, QWidget* parent)
    : SectionPanel(new SectionToolModel(tool), parent) {
  m_model->setParent(this);
}

SectionPanel::SectionPanel(SectionModel* model, QWidget* parent) : QWidget(parent), m_model(model) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(6, 6, 6, 6);
  outer->setSpacing(6);

  for (int i = 0; i < 3; ++i) {
    auto* box = new QWidget(this);
    auto* grid = new QGridLayout(box);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setVerticalSpacing(3);
    buildRow(i, grid);
    outer->addWidget(box);
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    outer->addWidget(line);
  }

  auto* bottom = new QHBoxLayout();
  m_handles = new QCheckBox(tr("Show handles"), this);
  m_handles->setToolTip(tr("Show the draggable plane handles in the 3D view."));
  auto* reset = new QPushButton(tr("Reset"), this);
  reset->setToolTip(tr("Turn off all section planes."));
  bottom->addWidget(m_handles);
  bottom->addStretch(1);
  bottom->addWidget(reset);
  outer->addLayout(bottom);
  outer->addStretch(1);

  connect(m_handles, &QCheckBox::toggled, this, [this](bool on) {
    if (m_busy) {
      return;
    }
    m_model->setHandlesVisible(on);
    Q_EMIT sectionChanged();
  });
  connect(reset, &QPushButton::clicked, this, &SectionPanel::onReset);
  connect(m_model, &SectionModel::planeChanged, this, &SectionPanel::onToolChanged);

  refresh();
}

void SectionPanel::buildRow(int index, QGridLayout* layout) {
  const QString cutTip = tr("The side the arrow points to is cut away.");
  Row& r = m_rows[static_cast<size_t>(index)];
  r.enable = new QCheckBox(tr("Plane %1").arg(index + 1), this);
  r.enable->setToolTip(tr("Cut the model with this plane. %1").arg(cutTip));
  r.controls = new QWidget(this);
  auto* inner = new QGridLayout(r.controls);
  inner->setContentsMargins(0, 0, 0, 0);
  inner->setVerticalSpacing(3);

  r.axis = new QComboBox(r.controls);
  r.axis->addItems({tr("X"), tr("Y"), tr("Z")});
  r.axis->setToolTip(tr("Axis the plane is perpendicular to."));
  r.flip = new QPushButton(tr("Flip"), r.controls);
  r.flip->setToolTip(tr("Cut away the other side of the plane. %1").arg(cutTip));

  r.slider = new QSlider(Qt::Horizontal, r.controls);
  r.slider->setRange(0, kSliderSteps);
  r.slider->setToolTip(tr("Position of the plane along its normal. %1").arg(cutTip));
  r.offset = new QDoubleSpinBox(r.controls);
  r.offset->setDecimals(2);
  r.offset->setSuffix(tr(" mm"));
  r.offset->setKeyboardTracking(false);
  r.offset->setToolTip(r.slider->toolTip());

  r.cap = new QCheckBox(tr("Cap"), r.controls);
  r.cap->setToolTip(tr("Fill the cut solids so sections look closed."));
  r.hatch = new QCheckBox(tr("Hatch"), r.controls);
  r.hatch->setToolTip(tr("Hatch the cut faces (needs Cap)."));

  inner->addWidget(r.axis, 0, 0);
  inner->addWidget(r.flip, 0, 1);
  inner->addWidget(r.cap, 0, 2);
  inner->addWidget(r.hatch, 0, 3);
  inner->addWidget(r.slider, 1, 0, 1, 3);
  inner->addWidget(r.offset, 1, 3);
  inner->setColumnStretch(2, 1);

  layout->addWidget(r.enable, 0, 0);
  layout->addWidget(r.controls, 1, 0);

  connect(r.enable, &QCheckBox::toggled, this, [this, index](bool on) {
    if (!m_busy) {
      edit(index, [on](render::SectionPlane& p) { p.enabled = on; });
    }
  });
  connect(r.axis, &QComboBox::activated, this, [this, index](int) { onAxisChosen(index); });
  connect(r.flip, &QPushButton::clicked, this, [this, index] { onFlip(index); });
  connect(r.slider, &QSlider::valueChanged, this, [this, index](int step) {
    if (!m_busy) {
      const auto [low, high] = m_model->offsetRange(index);
      setOffset(index, low + (high - low) * step / double(kSliderSteps));
    }
  });
  connect(r.offset, &QDoubleSpinBox::valueChanged, this, [this, index](double v) {
    if (!m_busy) {
      setOffset(index, v);
    }
  });
  connect(r.cap, &QCheckBox::toggled, this, [this, index](bool on) {
    if (!m_busy) {
      edit(index, [on](render::SectionPlane& p) { p.capping = on; });
    }
  });
  connect(r.hatch, &QCheckBox::toggled, this, [this, index](bool on) {
    if (!m_busy) {
      edit(index, [on](render::SectionPlane& p) { p.hatch = on; });
    }
  });
}

template <typename Fn>
void SectionPanel::edit(int index, Fn&& change) {
  render::SectionPlane p = m_model->plane(index);
  change(p);
  m_busy = true;
  m_model->setPlane(index, p);
  m_busy = false;
  refreshRow(index);
  Q_EMIT sectionChanged();
}

void SectionPanel::setOffset(int index, double offset) {
  const auto [low, high] = m_model->offsetRange(index);
  offset = std::clamp(offset, std::min(low, high), std::max(low, high));
  edit(index, [offset](render::SectionPlane& p) { p.offset = offset; });
}

void SectionPanel::onAxisChosen(int index) {
  const int axis = m_rows[static_cast<size_t>(index)].axis->currentIndex();
  if (axis < 0 || axis > 2) {
    return; // "Custom" is only a display state
  }
  const render::SectionPlane current = m_model->plane(index);
  // Keep the cut direction (sign) of an axis-aligned plane; custom normals start positive.
  const int old = axisOf(current.normal);
  const double oldComponents[3] = {current.normal.X(), current.normal.Y(), current.normal.Z()};
  const double sign = (old >= 0 && oldComponents[old] < 0) ? -1.0 : 1.0;
  const gp_Dir normal(axis == 0 ? sign : 0.0, axis == 1 ? sign : 0.0, axis == 2 ? sign : 0.0);
  if (normal.IsEqual(current.normal, 1e-9)) {
    return;
  }
  // The range depends on the normal, so apply it first, then pull the offset into the new range.
  edit(index, [&normal](render::SectionPlane& p) { p.normal = normal; });
  const auto [low, high] = m_model->offsetRange(index);
  const double offset = m_model->plane(index).offset;
  const double clamped = std::clamp(offset, std::min(low, high), std::max(low, high));
  if (clamped != offset) {
    setOffset(index, clamped);
  }
}

void SectionPanel::onFlip(int index) {
  m_busy = true;
  m_model->flip(index);
  m_busy = false;
  refreshRow(index);
  Q_EMIT sectionChanged();
}

void SectionPanel::onReset() {
  m_busy = true;
  m_model->reset();
  m_busy = false;
  refresh();
  Q_EMIT sectionChanged();
}

void SectionPanel::onToolChanged(int index) {
  if (m_busy || index < 0 || index >= 3) {
    return;
  }
  refreshRow(index);
}

void SectionPanel::refresh() {
  for (int i = 0; i < 3; ++i) {
    refreshRow(i);
  }
  const QSignalBlocker blocker(m_handles);
  m_handles->setChecked(m_model->handlesVisible());
}

void SectionPanel::refreshRow(int index) {
  Row& r = m_rows[static_cast<size_t>(index)];
  const render::SectionPlane& p = m_model->plane(index);
  const auto [rangeLow, rangeHigh] = m_model->offsetRange(index);
  const double low = std::min(rangeLow, rangeHigh);
  const double high = std::max(rangeLow, rangeHigh);

  const bool wasBusy = m_busy;
  m_busy = true;
  const QSignalBlocker b1(r.enable), b2(r.axis), b3(r.slider), b4(r.offset), b5(r.cap), b6(r.hatch);

  r.enable->setChecked(p.enabled);
  r.controls->setEnabled(p.enabled);

  const int axis = axisOf(p.normal);
  if (axis >= 0) {
    if (r.axis->count() > kCustomItem) {
      r.axis->removeItem(kCustomItem);
    }
    r.axis->setCurrentIndex(axis);
  } else {
    if (r.axis->count() <= kCustomItem) {
      r.axis->addItem(tr("Custom"));
    }
    r.axis->setCurrentIndex(kCustomItem);
  }

  // The spin box also covers an offset outside the model's range (it may be set programmatically).
  const double spinLow = std::min(low, p.offset);
  const double spinHigh = std::max(high, p.offset);
  r.offset->setRange(spinLow, spinHigh);
  r.offset->setSingleStep(std::max((spinHigh - spinLow) / 100.0, 0.01));
  r.offset->setValue(p.offset);

  const double span = high - low;
  const double t = span > 1e-12 ? (p.offset - low) / span : 0.5;
  r.slider->setValue(static_cast<int>(std::lround(std::clamp(t, 0.0, 1.0) * kSliderSteps)));

  r.cap->setChecked(p.capping);
  r.hatch->setChecked(p.hatch);
  r.hatch->setEnabled(p.enabled && p.capping);
  m_busy = wasBusy;
}

} // namespace sc::app
