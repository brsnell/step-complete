#include "app/AssemblyFilterProxy.h"

#include "app/AssemblyTreeModel.h"

namespace sc::app {

AssemblyFilterProxy::AssemblyFilterProxy(QObject* parent) : QSortFilterProxyModel(parent) {
  setRecursiveFilteringEnabled(true); // keeps the parents of every match
}

void AssemblyFilterProxy::setFilterText(const QString& text) {
  const QString trimmed = text.trimmed();
  if (trimmed == _text) {
    return;
  }
  _text = trimmed;
  invalidateFilter();
}

bool AssemblyFilterProxy::matches(const QModelIndex& sourceIndex) const {
  if (_text.isEmpty()) {
    return false;
  }
  const auto* tree = qobject_cast<const AssemblyTreeModel*>(sourceModel());
  if (tree == nullptr || !tree->assembly()) {
    return false;
  }
  const core::NodeId id = tree->nodeId(sourceIndex);
  if (id == core::NoNode) {
    return false;
  }
  const core::AssemblyNode& node = tree->assembly()->node(id);
  return QString::fromStdString(node.name).contains(_text, Qt::CaseInsensitive) ||
         QString::fromStdString(node.prototypeName).contains(_text, Qt::CaseInsensitive);
}

bool AssemblyFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
  if (_text.isEmpty()) {
    return true;
  }
  if (matches(sourceModel()->index(sourceRow, 0, sourceParent))) {
    return true;
  }
  // Everything below a matching assembly stays, so it can be expanded to its parts.
  for (QModelIndex ancestor = sourceParent; ancestor.isValid(); ancestor = ancestor.parent()) {
    if (matches(ancestor)) {
      return true;
    }
  }
  return false; // recursive filtering still keeps this row if a row below it matches
}

} // namespace sc::app
