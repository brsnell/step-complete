#pragma once

#include "core/Clash.h"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;

namespace sc::app {

// Measure > Check Clashes (docs/designs/clash-detection.md): what to report besides interference.
// Remembers the choices in QSettings under "clash/".
class ClashDialog : public QDialog {
  Q_OBJECT

public:
  explicit ClashDialog(QWidget* parent = nullptr);

  core::ClashOptions options() const;
  void saveSettings() const;

private:
  QCheckBox* _touching = nullptr;
  QCheckBox* _clearance = nullptr;
  QDoubleSpinBox* _clearanceValue = nullptr;
};

} // namespace sc::app
