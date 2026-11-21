#pragma once

#include "render/ViewSettings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace sc::app {

// The 3D view preferences, persisted in QSettings under "view/...".
struct ViewPreferences {
  render::BackgroundStyle background;
  render::LightingPreset lighting = render::LightingPreset::Default;
  bool antialiasing = false;

  static ViewPreferences load();
  void save() const;

  bool operator==(const ViewPreferences& other) const {
    return background == other.background && lighting == other.lighting && antialiasing == other.antialiasing;
  }
  bool operator!=(const ViewPreferences& other) const { return !(*this == other); }
};

// Modal preferences dialog. Every edit is previewed at once through preferencesChanged();
// Cancel restores the values the dialog was opened with. The caller saves on accept.
class PreferencesDialog : public QDialog {
  Q_OBJECT

public:
  explicit PreferencesDialog(const ViewPreferences& initial, QWidget* parent = nullptr);

  ViewPreferences preferences() const { return _current; }
  // Replaces the shown values (emits preferencesChanged if they differ).
  void setPreferences(const ViewPreferences& preferences);

Q_SIGNALS:
  void preferencesChanged(const ViewPreferences& preferences);

public:
  void accept() override;
  void reject() override;

private:
  void chooseColor(QColor& target, const QString& title);
  void updateWidgets();
  void edited();
  void updateCacheSize();

  ViewPreferences _initial;
  ViewPreferences _current;
  QComboBox* _styleCombo = nullptr;
  QPushButton* _topButton = nullptr;
  QPushButton* _bottomButton = nullptr;
  QPushButton* _solidButton = nullptr;
  QComboBox* _lightingCombo = nullptr;
  QCheckBox* _antialiasing = nullptr;
  QCheckBox* _cacheEnabled = nullptr;
  QLabel* _cacheSize = nullptr;
  QPushButton* _clearCache = nullptr;
};

} // namespace sc::app
