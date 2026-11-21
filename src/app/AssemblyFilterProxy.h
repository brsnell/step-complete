#pragma once

#include <QSortFilterProxyModel>
#include <QString>

namespace sc::app {

// Filters an AssemblyTreeModel by text, case-insensitively, against each row's instance
// name and product name. A row is kept when it matches, when a row below it matches
// (so the path to every match stays visible), or when a row above it matches (so a
// matching assembly can still be expanded to its parts).
class AssemblyFilterProxy : public QSortFilterProxyModel {
  Q_OBJECT

public:
  explicit AssemblyFilterProxy(QObject* parent = nullptr);

  void setFilterText(const QString& text);
  const QString& filterText() const { return _text; }
  bool filtering() const { return !_text.isEmpty(); }

  // Whether the source row itself matches the filter text (false when not filtering).
  bool matches(const QModelIndex& sourceIndex) const;

protected:
  bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
  QString _text;
};

} // namespace sc::app
