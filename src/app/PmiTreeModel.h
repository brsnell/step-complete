#pragma once

#include "core/PmiModel.h"

#include <QAbstractItemModel>
#include <QMetaType>

#include <memory>
#include <unordered_map>
#include <vector>

Q_DECLARE_METATYPE(std::vector<sc::core::PmiId>)

namespace sc::app {

// Qt item model over a PmiModel. Top-level rows are groups, in this order
// (empty groups are omitted):
//
//   Saved Views (n)          - one row per saved view; no check box
//   Dimensions (n)           - semantic dimensions
//   Geometric Tolerances (n) - semantic tolerances
//   Datums (n)               - semantic datums and datum targets
//   Not Interpreted (n)      - every item with semantic == false, whatever its kind
//
// An item appears in exactly one group: a not-interpreted dimension is listed
// only under "Not Interpreted", not under "Dimensions".
//
// Column 0 is the name, column 1 the display text. Every PMI row has a check
// box (visibility); group rows are tri-state and toggle all their children.
// The model owns the visibility state (by PmiId); all items start visible.
class PmiTreeModel : public QAbstractItemModel {
  Q_OBJECT

public:
  enum Group {
    SavedViewsGroup,
    DimensionsGroup,
    TolerancesGroup,
    DatumsGroup,
    NotInterpretedGroup,
  };
  Q_ENUM(Group)

  enum Roles {
    // PmiId of an item row (int); invalid for group and view rows.
    PmiIdRole = Qt::UserRole + 1,
    // Saved view id of a view row (int); invalid for group and item rows.
    ViewIdRole,
    // The Group (int) the row belongs to; valid on every row.
    GroupRole,
  };

  explicit PmiTreeModel(QObject* parent = nullptr);

  // Rebuilds the tree. Every item starts with the given visibility.
  void setModel(std::shared_ptr<const core::PmiModel> model, bool initiallyVisible = true);
  const std::shared_ptr<const core::PmiModel>& pmiModel() const { return m_pmi; }

  core::PmiId pmiId(const QModelIndex& index) const; // kNoPmi if not an item row
  int viewId(const QModelIndex& index) const;        // -1 if not a saved view row
  QModelIndex indexOf(core::PmiId id) const;
  QModelIndex indexOfView(int viewId) const;

  bool isVisible(core::PmiId id) const;
  // Programmatic change (e.g. a saved view was activated): updates the check
  // boxes with dataChanged but does not emit visibilityRequested.
  void setVisible(const std::vector<core::PmiId>& ids, bool visible);
  std::vector<core::PmiId> visibleIds() const; // ascending

  QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
  QModelIndex parent(const QModelIndex& child) const override;
  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

Q_SIGNALS:
  // The user toggled a check box. The model has already updated its own state.
  void visibilityRequested(std::vector<sc::core::PmiId> ids, bool visible);

private:
  enum class NodeType { Group, View, Item };

  struct Node {
    NodeType type = NodeType::Group;
    Group group = DimensionsGroup;
    int parent = -1;     // node index of the group (children only)
    int row = 0;         // row within the parent
    int payload = -1;    // PmiId or view id (children only)
    int firstChild = -1; // node index of the first child (groups only); children are contiguous
    int childCount = 0;
    int visibleCount = 0; // groups of items: visible children
  };

  const Node* nodeAt(const QModelIndex& index) const;
  QModelIndex indexOfNode(int node) const;
  QString groupTitle(const Node& group) const;
  Qt::CheckState groupCheckState(const Node& group) const;
  void emitGroupChanged(int groupNode);

  std::shared_ptr<const core::PmiModel> m_pmi;
  // Nodes 0..groupCount-1 are the groups in display order; each group's children follow contiguously.
  std::vector<Node> m_nodes;
  int m_groupCount = 0;
  std::vector<int> m_nodeOfItem; // per PmiId: node index
  std::unordered_map<int, int> m_nodeOfView;
  std::vector<bool> m_visible; // per PmiId
};

} // namespace sc::app
