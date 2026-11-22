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

#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>

namespace sc::app {

namespace {

constexpr int CustomItem = 3; // combo index of "Custom"

// Axis index (0..2) when the normal is parallel to a coordinate axis, else -1.
int axisOf(const gp_Dir& normal) {
  const double components[3] = {normal.X(), normal.Y(), normal.Z()};
  for (int i = 0; i < 3; ++i) {
    if (std::abs(std::abs(components[i]) - 1.0) < 1e-9) {
      return i;
    }
  }
  return -1;
}

} // namespace

SectionPanel::SectionPanel(render::SectionTool* tool, QWidget* parent)
    : SectionPanel(new SectionToolModel(tool), parent) {
  _model->setParent(this);
}

SectionPanel::SectionPanel(SectionModel* model, QWidget* parent) : QWidget(parent), _model(model) {
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
  _handles = new QCheckBox(tr("Show handles"), this);
  _handles->setToolTip(tr("Show the draggable plane handles in the 3D view."));
  auto* reset = new QPushButton(tr("Reset"), this);
  reset->setToolTip(tr("Turn off all section planes."));
  bottom->addWidget(_handles);
  bottom->addStretch(1);
  bottom->addWidget(reset);
  outer->addLayout(bottom);
  outer->addStretch(1);

  connect(_handles, &QCheckBox::toggled, this, [this](bool on) {
    if (_busy) {
      return;
    }
    _model->setHandlesVisible(on);
    Q_EMIT sectionChanged();
  });
  connect(reset, &QPushButton::clicked, this, &SectionPanel::onReset);
  connect(_model, &SectionModel::planeChanged, this, &SectionPanel::onToolChanged);
  connect(_model, &SectionModel::pickChanged, this, &SectionPanel::refreshPickButtons);

  refresh();
}

void SectionPanel::buildRow(int index, QGridLayout* layout) {
  const QString cutTip = tr("The side the arrow points to is cut away.");
  Row& row = _rows[static_cast<size_t>(index)];
  row.enable = new QCheckBox(tr("Plane %1").arg(index + 1), this);
  row.enable->setToolTip(tr("Cut the model with this plane. %1").arg(cutTip));
  row.controls = new QWidget(this);
  auto* inner = new QGridLayout(row.controls);
  inner->setContentsMargins(0, 0, 0, 0);
  inner->setVerticalSpacing(3);

  row.pick = new QPushButton(tr("Pick Face"), this);
  row.pick->setCheckable(true);
  row.pick->setObjectName(QStringLiteral("sectionPickFace"));
  row.pick->setToolTip(tr("Click a flat face in the 3D view to put this plane on it. %1").arg(cutTip));

  row.axis = new QComboBox(row.controls);
  row.axis->addItems({tr("X"), tr("Y"), tr("Z"), tr("Custom")});
  row.axis->setToolTip(tr("Axis the plane is perpendicular to, or Custom for any direction."));

  row.custom = new QWidget(row.controls);
  auto* customLayout = new QHBoxLayout(row.custom);
  customLayout->setContentsMargins(0, 0, 0, 0);
  const QString axisNames[3] = {tr("X"), tr("Y"), tr("Z")};
  for (int axis = 0; axis < 3; ++axis) {
    auto* spin = new QDoubleSpinBox(row.custom);
    spin->setRange(-1000.0, 1000.0);
    spin->setDecimals(3);
    spin->setSingleStep(0.1);
    spin->setKeyboardTracking(false);
    spin->setPrefix(axisNames[axis] + QStringLiteral(" "));
    spin->setObjectName(QStringLiteral("sectionNormal%1").arg(QLatin1Char("XYZ"[axis])));
    spin->setToolTip(tr("Direction the plane faces (any length; it is normalized)."));
    row.normal[static_cast<size_t>(axis)] = spin;
    customLayout->addWidget(spin);
  }
  row.custom->setVisible(false);
  row.flip = new QPushButton(tr("Flip"), row.controls);
  row.flip->setToolTip(tr("Cut away the other side of the plane. %1").arg(cutTip));

  row.slider = new QSlider(Qt::Horizontal, row.controls);
  row.slider->setRange(0, SliderSteps);
  row.slider->setToolTip(tr("Position of the plane along its normal. %1").arg(cutTip));
  row.offset = new QDoubleSpinBox(row.controls);
  row.offset->setObjectName(QStringLiteral("sectionOffset"));
  row.offset->setDecimals(2);
  row.offset->setSuffix(tr(" mm"));
  row.offset->setKeyboardTracking(false);
  row.offset->setToolTip(row.slider->toolTip());

  row.cap = new QCheckBox(tr("Cap"), row.controls);
  row.cap->setToolTip(tr("Fill the cut solids so sections look closed."));
  row.hatch = new QCheckBox(tr("Hatch"), row.controls);
  row.hatch->setToolTip(tr("Hatch the cut faces (needs Cap)."));

  inner->addWidget(row.axis, 0, 0);
  inner->addWidget(row.flip, 0, 1);
  inner->addWidget(row.cap, 0, 2);
  inner->addWidget(row.hatch, 0, 3);
  inner->addWidget(row.custom, 1, 0, 1, 4);
  inner->addWidget(row.slider, 2, 0, 1, 3);
  inner->addWidget(row.offset, 2, 3);
  inner->setColumnStretch(2, 1);

  auto* header = new QHBoxLayout();
  header->addWidget(row.enable);
  header->addStretch(1);
  header->addWidget(row.pick);
  layout->addLayout(header, 0, 0);
  layout->addWidget(row.controls, 1, 0);

  connect(row.enable, &QCheckBox::toggled, this, [this, index](bool on) {
    if (!_busy) {
      edit(index, [on](render::SectionPlane& p) { p.enabled = on; });
    }
  });
  connect(row.axis, &QComboBox::activated, this, [this, index](int) { onAxisChosen(index); });
  connect(row.pick, &QPushButton::clicked, this, [this, index] { onPickClicked(index); });
  for (QDoubleSpinBox* spin : row.normal) {
    connect(spin, &QDoubleSpinBox::valueChanged, this, [this, index](double) {
      if (!_busy) {
        onNormalEdited(index);
      }
    });
  }
  connect(row.flip, &QPushButton::clicked, this, [this, index] { onFlip(index); });
  connect(row.slider, &QSlider::valueChanged, this, [this, index](int step) {
    if (!_busy) {
      const auto [low, high] = _model->offsetRange(index);
      setOffset(index, low + (high - low) * step / double(SliderSteps));
    }
  });
  connect(row.offset, &QDoubleSpinBox::valueChanged, this, [this, index](double v) {
    if (!_busy) {
      setOffset(index, v);
    }
  });
  connect(row.cap, &QCheckBox::toggled, this, [this, index](bool on) {
    if (!_busy) {
      edit(index, [on](render::SectionPlane& p) { p.capping = on; });
    }
  });
  connect(row.hatch, &QCheckBox::toggled, this, [this, index](bool on) {
    if (!_busy) {
      edit(index, [on](render::SectionPlane& p) { p.hatch = on; });
    }
  });
}

template <typename Fn>
void SectionPanel::edit(int index, Fn&& change) {
  render::SectionPlane plane = _model->plane(index);
  change(plane);
  _busy = true;
  _model->setPlane(index, plane);
  _busy = false;
  refreshRow(index);
  Q_EMIT sectionChanged();
}

void SectionPanel::setOffset(int index, double offset) {
  const auto [low, high] = _model->offsetRange(index);
  offset = std::clamp(offset, std::min(low, high), std::max(low, high));
  edit(index, [offset](render::SectionPlane& p) { p.offset = offset; });
}

void SectionPanel::onAxisChosen(int index) {
  Row& row = _rows[static_cast<size_t>(index)];
  const int axis = row.axis->currentIndex();
  if (axis == CustomItem) {
    row.customChosen = true; // show the fields; the direction changes when they are edited
    refreshRow(index);
    return;
  }
  if (axis < 0 || axis > 2) {
    return;
  }
  row.customChosen = false;
  const render::SectionPlane current = _model->plane(index);
  // Keep the cut direction (sign) of an axis-aligned plane; custom normals start positive.
  const int old = axisOf(current.normal);
  const double oldComponents[3] = {current.normal.X(), current.normal.Y(), current.normal.Z()};
  const double sign = (old >= 0 && oldComponents[old] < 0) ? -1.0 : 1.0;
  const gp_Dir normal(axis == 0 ? sign : 0.0, axis == 1 ? sign : 0.0, axis == 2 ? sign : 0.0);
  if (normal.IsEqual(current.normal, 1e-9)) {
    refreshRow(index); // hides the custom fields
    return;
  }
  setNormal(index, normal);
}

void SectionPanel::onNormalEdited(int index) {
  const Row& row = _rows[static_cast<size_t>(index)];
  const gp_Vec vector(row.normal[0]->value(), row.normal[1]->value(), row.normal[2]->value());
  if (vector.Magnitude() < 1e-9) {
    refreshRow(index); // no direction: put the fields back
    return;
  }
  const gp_Dir normal(vector);
  if (normal.IsEqual(_model->plane(index).normal, 1e-12)) {
    return;
  }
  setNormal(index, normal);
}

void SectionPanel::setNormal(int index, const gp_Dir& normal) {
  // The range depends on the normal, so apply it first, then pull the offset into the new range.
  edit(index, [&normal](render::SectionPlane& p) { p.normal = normal; });
  const auto [low, high] = _model->offsetRange(index);
  const double offset = _model->plane(index).offset;
  const double clamped = std::clamp(offset, std::min(low, high), std::max(low, high));
  if (clamped != offset) {
    setOffset(index, clamped);
  }
}

void SectionPanel::onPickClicked(int index) {
  if (_model->pickingPlane() == index) {
    _model->cancelPick();
  } else {
    _model->pickFace(index);
  }
  refreshPickButtons();
}

void SectionPanel::refreshPickButtons() {
  for (int i = 0; i < 3; ++i) {
    QPushButton* pick = _rows[static_cast<size_t>(i)].pick;
    const QSignalBlocker blocker(pick);
    pick->setChecked(_model->pickingPlane() == i);
  }
}

void SectionPanel::onFlip(int index) {
  _busy = true;
  _model->flip(index);
  _busy = false;
  refreshRow(index);
  Q_EMIT sectionChanged();
}

void SectionPanel::onReset() {
  _busy = true;
  _model->reset();
  _busy = false;
  refresh();
  Q_EMIT sectionChanged();
}

void SectionPanel::onToolChanged(int index) {
  if (_busy || index < 0 || index >= 3) {
    return;
  }
  refreshRow(index);
}

void SectionPanel::refresh() {
  for (int i = 0; i < 3; ++i) {
    refreshRow(i);
  }
  refreshPickButtons();
  const QSignalBlocker blocker(_handles);
  _handles->setChecked(_model->handlesVisible());
}

void SectionPanel::refreshRow(int index) {
  Row& row = _rows[static_cast<size_t>(index)];
  const render::SectionPlane& plane = _model->plane(index);
  const auto [rangeLow, rangeHigh] = _model->offsetRange(index);
  const double low = std::min(rangeLow, rangeHigh);
  const double high = std::max(rangeLow, rangeHigh);

  const bool wasBusy = _busy;
  _busy = true;
  const QSignalBlocker blockEnable(row.enable), blockAxis(row.axis), blockSlider(row.slider),
      blockOffset(row.offset), blockCap(row.cap), blockHatch(row.hatch);

  row.enable->setChecked(plane.enabled);
  row.controls->setEnabled(plane.enabled);

  const int axis = axisOf(plane.normal);
  const bool custom = axis < 0 || row.customChosen;
  row.axis->setCurrentIndex(custom ? CustomItem : axis);
  row.custom->setVisible(custom);
  const double components[3] = {plane.normal.X(), plane.normal.Y(), plane.normal.Z()};
  for (int i = 0; i < 3; ++i) {
    QDoubleSpinBox* spin = row.normal[static_cast<size_t>(i)];
    const QSignalBlocker blockSpin(spin);
    spin->setValue(components[i]);
  }

  // The spin box also covers an offset outside the model's range (it may be set programmatically).
  const double spinLow = std::min(low, plane.offset);
  const double spinHigh = std::max(high, plane.offset);
  row.offset->setRange(spinLow, spinHigh);
  row.offset->setSingleStep(std::max((spinHigh - spinLow) / 100.0, 0.01));
  row.offset->setValue(plane.offset);

  const double span = high - low;
  const double fraction = span > 1e-12 ? (plane.offset - low) / span : 0.5;
  row.slider->setValue(static_cast<int>(std::lround(std::clamp(fraction, 0.0, 1.0) * SliderSteps)));

  row.cap->setChecked(plane.capping);
  row.hatch->setChecked(plane.hatch);
  row.hatch->setEnabled(plane.enabled && plane.capping);
  _busy = wasBusy;
}

} // namespace sc::app
