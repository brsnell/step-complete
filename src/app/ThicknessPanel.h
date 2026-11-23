#pragma once

#include "core/Measurement.h"
#include "core/WallThickness.h"

#include <QWidget>

class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QToolButton;

namespace sc::app {

// The Wall Thickness dock (Measure > Wall Thickness, docs/designs/wall-thickness.md): the range
// found, the thin and thick limits of the color scale, and progress with Cancel while measuring.
class ThicknessPanel : public QWidget {
  Q_OBJECT

public:
  explicit ThicknessPanel(QWidget* parent = nullptr);

  void setRunning(int percent, const QString& stage);
  // Shows a finished result; the limits start at its range. Values use the units.
  void setResult(const core::ThicknessResult& result, const core::MeasureDisplayOptions& units);
  void clear();

  bool isRunning() const { return _running; }
  QString summary() const;
  // In mm.
  double thinLimit() const;
  double thickLimit() const;
  void setLimits(double thin, double thick);

  static QString summaryOf(const core::ThicknessResult& result, const core::MeasureDisplayOptions& units);

Q_SIGNALS:
  void cancelRequested();
  // The user changed a limit (mm).
  void limitsChanged(double thin, double thick);

private:
  void emitLimits();

  QLabel* _summary = nullptr;
  QProgressBar* _progress = nullptr;
  QToolButton* _cancel = nullptr;
  QDoubleSpinBox* _thin = nullptr;
  QDoubleSpinBox* _thick = nullptr;
  QToolButton* _reset = nullptr;
  QWidget* _limits = nullptr;
  double _perUnit = 1.0; // mm per displayed unit
  double _foundMin = 0.0;
  double _foundMax = 0.0;
  bool _running = false;
  bool _updating = false;
};

} // namespace sc::app
