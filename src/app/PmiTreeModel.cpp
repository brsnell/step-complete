#include "app/PmiTreeModel.h"

#include <QFont>

#include <algorithm>
#include <array>

namespace sc::app {

namespace {

constexpr int GroupKinds = 5;

PmiTreeModel::Group groupOf(const core::PmiItem& item) {
  if (!item.semantic) {
    return PmiTreeModel::NotInterpretedGroup;
  }
  switch (item.kind) {
  case core::PmiKind::Dimension:
    return PmiTreeModel::DimensionsGroup;
  case core::PmiKind::GeometricTolerance:
    return PmiTreeModel::TolerancesGroup;
  case core::PmiKind::Datum:
  case core::PmiKind::DatumTarget:
    return PmiTreeModel::DatumsGroup;
  }
  return PmiTreeModel::NotInterpretedGroup;
}

QString qs(const std::string& text) {
  return QString::fromStdString(text);
}

} // namespace

PmiTreeModel::PmiTreeModel(QObject* parent) : QAbstractItemModel(parent) {}

void PmiTreeModel::setModel(std::shared_ptr<const core::PmiModel> model, bool initiallyVisible) {
  beginResetModel();
  _pmi = std::move(model);
  _nodes.clear();
  _nodeOfItem.clear();
  _nodeOfView.clear();
  _visible.clear();
  _groupCount = 0;

  if (_pmi) {
    const auto& items = _pmi->items();
    const auto& views = _pmi->views();
    _nodeOfItem.assign(items.size(), -1);
    _visible.assign(items.size(), initiallyVisible);

    std::array<std::vector<int>, GroupKinds> members; // item ids per group
    for (size_t i = 0; i < items.size(); ++i) {
      members[static_cast<size_t>(groupOf(items[i]))].push_back(static_cast<int>(i));
    }

    // Group nodes first, in display order; omit empty groups.
    for (int groupNumber = 0; groupNumber < GroupKinds; ++groupNumber) {
      const size_t count =
          groupNumber == SavedViewsGroup ? views.size() : members[static_cast<size_t>(groupNumber)].size();
      if (count == 0) {
        continue;
      }
      Node group;
      group.type = NodeType::Group;
      group.group = static_cast<Group>(groupNumber);
      group.row = _groupCount++;
      group.childCount = static_cast<int>(count);
      _nodes.push_back(group);
    }
    // Then each group's children, contiguously.
    _nodes.reserve(_nodes.size() + items.size() + views.size());
    for (int groupNode = 0; groupNode < _groupCount; ++groupNode) {
      const Group groupKind = _nodes[static_cast<size_t>(groupNode)].group;
      _nodes[static_cast<size_t>(groupNode)].firstChild = static_cast<int>(_nodes.size());
      if (groupKind == SavedViewsGroup) {
        for (size_t row = 0; row < views.size(); ++row) {
          Node node;
          node.type = NodeType::View;
          node.group = groupKind;
          node.parent = groupNode;
          node.row = static_cast<int>(row);
          node.payload = views[row].id;
          _nodeOfView[views[row].id] = static_cast<int>(_nodes.size());
          _nodes.push_back(node);
        }
        continue;
      }
      const std::vector<int>& ids = members[static_cast<size_t>(groupKind)];
      for (size_t row = 0; row < ids.size(); ++row) {
        Node node;
        node.type = NodeType::Item;
        node.group = groupKind;
        node.parent = groupNode;
        node.row = static_cast<int>(row);
        node.payload = ids[row];
        _nodeOfItem[static_cast<size_t>(ids[row])] = static_cast<int>(_nodes.size());
        _nodes.push_back(node);
      }
      _nodes[static_cast<size_t>(groupNode)].visibleCount =
          initiallyVisible ? static_cast<int>(ids.size()) : 0;
    }
  }
  endResetModel();
}

const PmiTreeModel::Node* PmiTreeModel::nodeAt(const QModelIndex& index) const {
  if (!index.isValid() || index.internalId() == 0) {
    return nullptr;
  }
  return &_nodes[static_cast<size_t>(index.internalId() - 1)];
}

QModelIndex PmiTreeModel::indexOfNode(int node) const {
  if (node < 0 || static_cast<size_t>(node) >= _nodes.size()) {
    return {};
  }
  return createIndex(_nodes[static_cast<size_t>(node)].row, 0, static_cast<quintptr>(node) + 1);
}

core::PmiId PmiTreeModel::pmiId(const QModelIndex& index) const {
  const Node* node = nodeAt(index);
  return node && node->type == NodeType::Item ? node->payload : core::NoPmi;
}

int PmiTreeModel::viewId(const QModelIndex& index) const {
  const Node* node = nodeAt(index);
  return node && node->type == NodeType::View ? node->payload : -1;
}

QModelIndex PmiTreeModel::indexOf(core::PmiId id) const {
  if (id < 0 || static_cast<size_t>(id) >= _nodeOfItem.size()) {
    return {};
  }
  return indexOfNode(_nodeOfItem[static_cast<size_t>(id)]);
}

QModelIndex PmiTreeModel::indexOfView(int viewId) const {
  const auto it = _nodeOfView.find(viewId);
  return it == _nodeOfView.end() ? QModelIndex() : indexOfNode(it->second);
}

bool PmiTreeModel::isVisible(core::PmiId id) const {
  return id >= 0 && static_cast<size_t>(id) < _visible.size() && _visible[static_cast<size_t>(id)];
}

std::vector<core::PmiId> PmiTreeModel::visibleIds() const {
  std::vector<core::PmiId> ids;
  for (size_t i = 0; i < _visible.size(); ++i) {
    if (_visible[i]) {
      ids.push_back(static_cast<core::PmiId>(i));
    }
  }
  return ids;
}

void PmiTreeModel::emitGroupChanged(int groupNode) {
  const Node& group = _nodes[static_cast<size_t>(groupNode)];
  const QModelIndex groupIndex = indexOfNode(groupNode);
  Q_EMIT dataChanged(groupIndex, groupIndex, {Qt::CheckStateRole});
  if (group.childCount > 0) {
    Q_EMIT dataChanged(index(0, 0, groupIndex), index(group.childCount - 1, 0, groupIndex),
                       {Qt::CheckStateRole});
  }
}

void PmiTreeModel::setVisible(const std::vector<core::PmiId>& ids, bool visible) {
  std::vector<bool> touched(static_cast<size_t>(_groupCount), false);
  for (const core::PmiId id : ids) {
    if (id < 0 || static_cast<size_t>(id) >= _visible.size() ||
        _visible[static_cast<size_t>(id)] == visible) {
      continue;
    }
    _visible[static_cast<size_t>(id)] = visible;
    const Node& item = _nodes[static_cast<size_t>(_nodeOfItem[static_cast<size_t>(id)])];
    _nodes[static_cast<size_t>(item.parent)].visibleCount += visible ? 1 : -1;
    touched[static_cast<size_t>(item.parent)] = true;
  }
  for (int groupNumber = 0; groupNumber < _groupCount; ++groupNumber) {
    if (touched[static_cast<size_t>(groupNumber)]) {
      emitGroupChanged(groupNumber);
    }
  }
}

QModelIndex PmiTreeModel::index(int row, int column, const QModelIndex& parent) const {
  if (row < 0 || column < 0 || column >= columnCount() || _nodes.empty()) {
    return {};
  }
  if (!parent.isValid()) {
    return row < _groupCount ? createIndex(row, column, static_cast<quintptr>(row) + 1) : QModelIndex();
  }
  const Node* group = nodeAt(parent);
  if (!group || group->type != NodeType::Group || parent.column() != 0 || row >= group->childCount) {
    return {};
  }
  return createIndex(row, column, static_cast<quintptr>(group->firstChild + row) + 1);
}

QModelIndex PmiTreeModel::parent(const QModelIndex& child) const {
  const Node* node = nodeAt(child);
  if (!node || node->parent < 0) {
    return {};
  }
  return indexOfNode(node->parent);
}

int PmiTreeModel::rowCount(const QModelIndex& parent) const {
  if (!parent.isValid()) {
    return _groupCount;
  }
  const Node* node = nodeAt(parent);
  return node && node->type == NodeType::Group && parent.column() == 0 ? node->childCount : 0;
}

int PmiTreeModel::columnCount(const QModelIndex&) const {
  return 2;
}

QString PmiTreeModel::groupTitle(const Node& group) const {
  QString title;
  switch (group.group) {
  case SavedViewsGroup:
    title = tr("Saved Views");
    break;
  case DimensionsGroup:
    title = tr("Dimensions");
    break;
  case TolerancesGroup:
    title = tr("Geometric Tolerances");
    break;
  case DatumsGroup:
    title = tr("Datums");
    break;
  case NotInterpretedGroup:
    title = tr("Not Interpreted");
    break;
  }
  return QStringLiteral("%1 (%2)").arg(title).arg(group.childCount);
}

Qt::CheckState PmiTreeModel::groupCheckState(const Node& group) const {
  if (group.visibleCount == 0) {
    return Qt::Unchecked;
  }
  return group.visibleCount == group.childCount ? Qt::Checked : Qt::PartiallyChecked;
}

QVariant PmiTreeModel::data(const QModelIndex& index, int role) const {
  const Node* node = nodeAt(index);
  if (!node) {
    return {};
  }
  if (role == GroupRole) {
    return static_cast<int>(node->group);
  }
  const int column = index.column();
  switch (node->type) {
  case NodeType::Group:
    switch (role) {
    case Qt::DisplayRole:
      return column == 0 ? QVariant(groupTitle(*node)) : QVariant();
    case Qt::FontRole:
      if (column == 0) {
        QFont font;
        font.setBold(true);
        return font;
      }
      return {};
    case Qt::CheckStateRole:
      if (column == 0 && node->group != SavedViewsGroup) {
        return groupCheckState(*node);
      }
      return {};
    default:
      return {};
    }
  case NodeType::View: {
    if (role == ViewIdRole) {
      return node->payload;
    }
    if (role == Qt::DisplayRole && column == 0) {
      return qs(_pmi->views()[static_cast<size_t>(node->row)].name); // views are listed in order
    }
    return {};
  }
  case NodeType::Item: {
    const core::PmiItem& item = _pmi->item(node->payload);
    switch (role) {
    case PmiIdRole:
      return node->payload;
    case Qt::DisplayRole:
      return column == 0 ? qs(item.name) : qs(item.text);
    case Qt::ToolTipRole: {
      QString tip = qs(item.typeName);
      if (!item.text.empty()) {
        tip += tip.isEmpty() ? qs(item.text) : QStringLiteral(": ") + qs(item.text);
      }
      return tip;
    }
    case Qt::CheckStateRole:
      if (column == 0) {
        return _visible[static_cast<size_t>(node->payload)] ? Qt::Checked : Qt::Unchecked;
      }
      return {};
    default:
      return {};
    }
  }
  }
  return {};
}

bool PmiTreeModel::setData(const QModelIndex& index, const QVariant& value, int role) {
  const Node* node = nodeAt(index);
  if (!node || role != Qt::CheckStateRole || index.column() != 0 || node->type == NodeType::View ||
      node->group == SavedViewsGroup) {
    return false;
  }
  // A partly checked group becomes fully checked when clicked.
  const bool visible = static_cast<Qt::CheckState>(value.toInt()) != Qt::Unchecked;
  std::vector<core::PmiId> ids;
  if (node->type == NodeType::Item) {
    ids.push_back(node->payload);
  } else {
    ids.reserve(static_cast<size_t>(node->childCount));
    for (int i = 0; i < node->childCount; ++i) {
      ids.push_back(_nodes[static_cast<size_t>(node->firstChild + i)].payload);
    }
  }
  setVisible(ids, visible);
  Q_EMIT visibilityRequested(std::move(ids), visible);
  return true;
}

Qt::ItemFlags PmiTreeModel::flags(const QModelIndex& index) const {
  const Node* node = nodeAt(index);
  if (!node) {
    return Qt::NoItemFlags;
  }
  Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  if (index.column() == 0 && node->group != SavedViewsGroup) {
    flags |= Qt::ItemIsUserCheckable;
  }
  return flags;
}

QVariant PmiTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
    if (section == 0) {
      return tr("Name");
    }
    if (section == 1) {
      return tr("Annotation");
    }
  }
  return {};
}

} // namespace sc::app
