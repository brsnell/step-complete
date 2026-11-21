#include "app/AssemblyTreeModel.h"

#include <QApplication>
#include <QStyle>

namespace sc::app {

namespace {

quintptr toInternalId(core::NodeId id) {
  return static_cast<quintptr>(id) + 1; // 0 is reserved for "no node"
}

} // namespace

AssemblyTreeModel::AssemblyTreeModel(QObject* parent) : QAbstractItemModel(parent) {
  QStyle* style = QApplication::style();
  _assemblyIcon = style->standardIcon(QStyle::SP_DirIcon);
  _partIcon = style->standardIcon(QStyle::SP_FileIcon);
}

void AssemblyTreeModel::setModel(std::shared_ptr<const core::AssemblyIndex> index,
                                 PartVisibility visibility) {
  beginResetModel();
  _index = std::move(index);
  _visibility = std::move(visibility);
  _rowInParent.clear();
  if (_index) {
    _rowInParent.assign(_index->nodes().size(), 0);
    for (size_t row = 0; row < _index->roots().size(); ++row) {
      _rowInParent[static_cast<size_t>(_index->roots()[row])] = static_cast<int>(row);
    }
    for (const auto& node : _index->nodes()) {
      for (size_t row = 0; row < node.children.size(); ++row) {
        _rowInParent[static_cast<size_t>(node.children[row])] = static_cast<int>(row);
      }
    }
  }
  computeVisibleCounts();
  endResetModel();
}

void AssemblyTreeModel::computeVisibleCounts() {
  _visibleParts.clear();
  _totalParts.clear();
  if (!_index) {
    return;
  }
  const auto& nodes = _index->nodes();
  _visibleParts.assign(nodes.size(), 0);
  _totalParts.assign(nodes.size(), 0);
  // Children always have larger ids than their parent (depth-first build), so a
  // reverse pass accumulates counts bottom-up.
  for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
    const auto id = static_cast<size_t>(it->id);
    if (it->kind == core::NodeKind::Part) {
      _totalParts[id] = 1;
      _visibleParts[id] = _visibility && _visibility(it->id) ? 1 : 0;
    }
    if (it->parent != core::NoNode) {
      _totalParts[static_cast<size_t>(it->parent)] += _totalParts[id];
      _visibleParts[static_cast<size_t>(it->parent)] += _visibleParts[id];
    }
  }
}

void AssemblyTreeModel::refreshVisibility() {
  if (!_index || _index->nodes().empty()) {
    return;
  }
  computeVisibleCounts();
  // A single top-level range is not enough for views; notify each parent's rows.
  std::vector<QModelIndex> parents{QModelIndex()};
  while (!parents.empty()) {
    const QModelIndex parent = parents.back();
    parents.pop_back();
    const int rows = rowCount(parent);
    if (rows == 0) {
      continue;
    }
    Q_EMIT dataChanged(index(0, 0, parent), index(rows - 1, 0, parent), {Qt::CheckStateRole});
    for (int row = 0; row < rows; ++row) {
      const QModelIndex child = index(row, 0, parent);
      if (rowCount(child) > 0) {
        parents.push_back(child);
      }
    }
  }
}

core::NodeId AssemblyTreeModel::nodeId(const QModelIndex& index) const {
  if (!index.isValid() || index.internalId() == 0) {
    return core::NoNode;
  }
  return static_cast<core::NodeId>(index.internalId() - 1);
}

QModelIndex AssemblyTreeModel::indexOf(core::NodeId node) const {
  if (!_index || node < 0 || static_cast<size_t>(node) >= _rowInParent.size()) {
    return {};
  }
  return createIndex(_rowInParent[static_cast<size_t>(node)], 0, toInternalId(node));
}

QModelIndex AssemblyTreeModel::index(int row, int column, const QModelIndex& parent) const {
  if (!_index || column != 0 || row < 0) {
    return {};
  }
  const std::vector<core::NodeId>& siblings =
      parent.isValid() ? _index->node(nodeId(parent)).children : _index->roots();
  if (static_cast<size_t>(row) >= siblings.size()) {
    return {};
  }
  return createIndex(row, column, toInternalId(siblings[static_cast<size_t>(row)]));
}

QModelIndex AssemblyTreeModel::parent(const QModelIndex& child) const {
  const core::NodeId id = nodeId(child);
  if (id == core::NoNode) {
    return {};
  }
  return indexOf(_index->node(id).parent);
}

int AssemblyTreeModel::rowCount(const QModelIndex& parent) const {
  if (!_index) {
    return 0;
  }
  if (!parent.isValid()) {
    return static_cast<int>(_index->roots().size());
  }
  if (parent.column() != 0) {
    return 0;
  }
  return static_cast<int>(_index->node(nodeId(parent)).children.size());
}

int AssemblyTreeModel::columnCount(const QModelIndex&) const {
  return 1;
}

QVariant AssemblyTreeModel::data(const QModelIndex& index, int role) const {
  const core::NodeId id = nodeId(index);
  if (id == core::NoNode) {
    return {};
  }
  const core::AssemblyNode& node = _index->node(id);
  switch (role) {
  case Qt::DisplayRole:
    return QString::fromStdString(node.name);
  case Qt::ToolTipRole:
    return QString::fromStdString(_index->displayPath(id));
  case Qt::DecorationRole:
    return node.kind == core::NodeKind::Assembly ? _assemblyIcon : _partIcon;
  case Qt::CheckStateRole: {
    const int visible = _visibleParts[static_cast<size_t>(id)];
    const int total = _totalParts[static_cast<size_t>(id)];
    if (visible == 0) {
      return Qt::Unchecked;
    }
    return visible == total ? Qt::Checked : Qt::PartiallyChecked;
  }
  default:
    return {};
  }
}

bool AssemblyTreeModel::setData(const QModelIndex& index, const QVariant& value, int role) {
  const core::NodeId id = nodeId(index);
  if (id == core::NoNode || role != Qt::CheckStateRole) {
    return false;
  }
  // Clicking a partly visible assembly shows everything below it.
  const auto state = static_cast<Qt::CheckState>(value.toInt());
  Q_EMIT visibilityRequested(id, state != Qt::Unchecked);
  return true;
}

Qt::ItemFlags AssemblyTreeModel::flags(const QModelIndex& index) const {
  if (!index.isValid()) {
    return Qt::NoItemFlags;
  }
  return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
}

QVariant AssemblyTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (section == 0 && orientation == Qt::Horizontal && role == Qt::DisplayRole) {
    return tr("Structure");
  }
  return {};
}

} // namespace sc::app
