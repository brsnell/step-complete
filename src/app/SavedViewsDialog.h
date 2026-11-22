#pragma once

#include "app/UserViews.h"

#include <QDialog>

#include <vector>

class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace sc::app {

// View > Saved Views > Manage Saved Views: rename (edit in place), delete and reorder the
// user's saved views. Works on a copy; views() has the result after OK.
class SavedViewsDialog : public QDialog {
  Q_OBJECT

public:
  explicit SavedViewsDialog(std::vector<UserView> views, QWidget* parent = nullptr);

  const std::vector<UserView>& views() const { return _views; }

  // The same actions as the buttons, on the current row (for tests).
  void renameCurrent(const QString& name);
  void deleteCurrent();
  void moveCurrent(int step);
  void setCurrentRow(int row);

private:
  void rebuild(int currentRow);
  void onItemChanged(QListWidgetItem* item);
  void updateButtons();

  std::vector<UserView> _views;
  QListWidget* _list = nullptr;
  QPushButton* _rename = nullptr;
  QPushButton* _delete = nullptr;
  QPushButton* _up = nullptr;
  QPushButton* _down = nullptr;
  bool _rebuilding = false;
};

} // namespace sc::app
