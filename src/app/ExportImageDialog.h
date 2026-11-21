#pragma once

#include "render/ViewSettings.h"

#include <QDialog>
#include <QSize>

class QCheckBox;
class QComboBox;
class QRadioButton;
class QSpinBox;

namespace sc::app {

// Options for File > Export Image: pixel size and background.
class ExportImageDialog : public QDialog {
  Q_OBJECT

public:
  // viewSize: the 3D view's size in pixels (the default export size).
  // allowTransparent: false for formats without alpha (JPEG).
  ExportImageDialog(const QSize& viewSize, int maxSize, bool allowTransparent, QWidget* parent = nullptr);

  QSize imageSize() const;
  render::ExportBackground background() const;

private:
  void onWidthEdited(int width);
  void onHeightEdited(int height);
  void updateEnabled();

  QSize _viewSize;
  QRadioButton* _currentSize = nullptr;
  QRadioButton* _customSize = nullptr;
  QSpinBox* _width = nullptr;
  QSpinBox* _height = nullptr;
  QCheckBox* _keepAspect = nullptr;
  QComboBox* _background = nullptr;
};

} // namespace sc::app
