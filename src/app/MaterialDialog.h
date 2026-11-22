#pragma once

#include "core/AssemblyIndex.h"

#include <QDialog>

#include <optional>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

namespace sc::app {

// Measure > Set Material (docs/designs/material-density.md): a material from a short list, or a
// custom name and density (kg/m³, with g/cm³ shown beside it).
class MaterialDialog : public QDialog {
  Q_OBJECT

public:
  // 'current' preselects a matching preset, or fills in the custom fields.
  explicit MaterialDialog(const std::optional<core::Material>& current = std::nullopt,
                          QWidget* parent = nullptr);

  core::Material material() const;

  // The preset list (name and kg/m³), in menu order.
  static const std::vector<core::Material>& presets();

private:
  void onPresetChosen(int index);
  void onEdited();
  void updateHint();

  QComboBox* _preset = nullptr;
  QLineEdit* _name = nullptr;
  QDoubleSpinBox* _density = nullptr;
  QLabel* _hint = nullptr;
  bool _filling = false;
};

} // namespace sc::app
