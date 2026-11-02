#pragma once

#include "core/AssemblyIndex.h"

#include <QAbstractItemModel>
#include <QIcon>

#include <functional>
#include <memory>
#include <vector>

namespace sc::app {

// Qt item model over an AssemblyIndex. The check box of each row shows and
// toggles visibility (tri-state for partly visible assemblies).
class AssemblyTreeModel : public QAbstractItemModel {
  Q_OBJECT

public:
  // Answers whether a part instance is currently shown.
  using PartVisibility = std::function<bool(core::NodeId part)>;

  explicit AssemblyTreeModel(QObject* parent = nullptr);

  void setModel(std::shared_ptr<const core::AssemblyIndex> index, PartVisibility visibility);
  const std::shared_ptr<const core::AssemblyIndex>& assembly() const { return m_index; }

  core::NodeId nodeId(const QModelIndex& index) const;
  QModelIndex indexOf(core::NodeId node) const;

  // Re-reads visibility for every row (call after hide/show/isolate).
  void refreshVisibility();

  QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
  QModelIndex parent(const QModelIndex& child) const override;
  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

Q_SIGNALS:
  // The user toggled a check box; the owner applies it and calls refreshVisibility().
  void visibilityRequested(core::NodeId node, bool visible);

private:
  void computeVisibleCounts();

  std::shared_ptr<const core::AssemblyIndex> m_index;
  PartVisibility m_visibility;
  std::vector<int> m_rowInParent;  // per node
  std::vector<int> m_visibleParts; // per node: visible part instances in subtree
  std::vector<int> m_totalParts;   // per node: part instances in subtree
  QIcon m_assemblyIcon;
  QIcon m_partIcon;
};

} // namespace sc::app
