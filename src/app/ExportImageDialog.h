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

  QSize m_viewSize;
  QRadioButton* m_currentSize = nullptr;
  QRadioButton* m_customSize = nullptr;
  QSpinBox* m_width = nullptr;
  QSpinBox* m_height = nullptr;
  QCheckBox* m_keepAspect = nullptr;
  QComboBox* m_background = nullptr;
};

} // namespace sc::app
