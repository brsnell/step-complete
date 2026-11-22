#pragma once

#include <QDialog>

class QCheckBox;
class QRadioButton;

namespace sc::app {

// File > Export Parts List (docs/designs/parts-list-export.md): which parts, and which optional
// columns. Remembers the choices in QSettings under "partsList/".
class PartsListDialog : public QDialog {
  Q_OBJECT

public:
  explicit PartsListDialog(QWidget* parent = nullptr);

  bool visibleOnly() const;
  bool volume() const;
  bool mass() const;
  bool boundingBox() const;

  void saveSettings() const;

private:
  QRadioButton* _all = nullptr;
  QRadioButton* _visible = nullptr;
  QCheckBox* _volume = nullptr;
  QCheckBox* _mass = nullptr;
  QCheckBox* _boundingBox = nullptr;
};

} // namespace sc::app
