#pragma once

#include "core/GeometryExport.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QRadioButton;

namespace sc::app {

// Options for File > Export Geometry (docs/designs/geometry-export.md): format, parts, mesh
// quality, unit and (OBJ) up axis. Remembers them in QSettings under "exportGeometry/".
class ExportGeometryDialog : public QDialog {
  Q_OBJECT

public:
  // hasSelection: offer "Selected parts".
  explicit ExportGeometryDialog(bool hasSelection, QWidget* parent = nullptr);

  core::GeometryExportOptions options() const;
  bool selectedPartsOnly() const;

  // Stores the current choices (done on accept).
  void saveSettings() const;

private:
  void loadSettings();
  void updateEnabled();

  bool _hasSelection = false;
  QComboBox* _format = nullptr;
  QRadioButton* _visibleParts = nullptr;
  QRadioButton* _selectedParts = nullptr;
  QComboBox* _quality = nullptr;
  QDoubleSpinBox* _tolerance = nullptr;
  QDoubleSpinBox* _angle = nullptr;
  QLabel* _memoryNote = nullptr;
  QComboBox* _unit = nullptr;
  QComboBox* _upAxis = nullptr;
};

} // namespace sc::app
