#pragma once

#include "app/PmiTreeModel.h"
#include "core/PmiModel.h"

#include <QWidget>

#include <memory>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QTreeView;

namespace sc::app {

class PmiFilterProxy;

// PMI browser: filter box, kind filter, tree of saved views / dimensions /
// tolerances / datums (see PmiTreeModel) and a summary line.
class PmiPanel : public QWidget {
  Q_OBJECT

public:
  explicit PmiPanel(QWidget* parent = nullptr);

  // Shows the PMI of a document; nullptr or an empty model shows "No PMI in this file".
  // Every annotation starts visible (see PmiTreeModel).
  void setModel(std::shared_ptr<const core::PmiModel> model);
  void clear();

  // Programmatic selection (e.g. from the 3D view): selects the rows of the
  // given PMI and scrolls to the first. Does not emit pmiSelected.
  void selectPmi(const std::vector<core::PmiId>& ids);

  // Programmatic visibility change: updates the check boxes, does not emit visibilityRequested.
  void setVisible(const std::vector<core::PmiId>& ids, bool visible);
  using QWidget::setVisible;
  std::vector<core::PmiId> visibleIds() const;

  const PmiTreeModel* treeModel() const { return m_model; }

Q_SIGNALS:
  // The selection changed by user action; ids of the selected PMI rows (groups and saved views excluded).
  void pmiSelected(std::vector<sc::core::PmiId> ids);
  // A saved view row was activated (double-click or Enter).
  void viewActivated(int viewId);
  // The user toggled check boxes; forwarded from the model.
  void visibilityRequested(std::vector<sc::core::PmiId> ids, bool visible);

private:
  void updateSummary();
  void applyFilter();
  void onSelectionChanged();
  void onActivated(const QModelIndex& proxyIndex);

  QLineEdit* m_search = nullptr;
  QComboBox* m_kind = nullptr;
  QTreeView* m_tree = nullptr;
  QLabel* m_summary = nullptr;
  PmiTreeModel* m_model = nullptr;
  PmiFilterProxy* m_proxy = nullptr;
  bool m_programmaticSelection = false;
};

} // namespace sc::app
