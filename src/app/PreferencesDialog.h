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

  ViewPreferences preferences() const { return m_current; }
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

  ViewPreferences m_initial;
  ViewPreferences m_current;
  QComboBox* m_styleCombo = nullptr;
  QPushButton* m_topButton = nullptr;
  QPushButton* m_bottomButton = nullptr;
  QPushButton* m_solidButton = nullptr;
  QComboBox* m_lightingCombo = nullptr;
  QCheckBox* m_antialiasing = nullptr;
  QCheckBox* m_cacheEnabled = nullptr;
  QLabel* m_cacheSize = nullptr;
  QPushButton* m_clearCache = nullptr;
};

} // namespace sc::app
