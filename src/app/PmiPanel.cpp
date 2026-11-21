#include "app/PmiPanel.h"

#include <QComboBox>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>

namespace sc::app {

namespace {

constexpr int AllKinds = -1;

} // namespace

// Filters by group (kind filter) and by name/text (case-insensitive). A group row is
// kept when its kind is allowed and, if a text filter is set, when a child matches.
class PmiFilterProxy : public QSortFilterProxyModel {
public:
  explicit PmiFilterProxy(QObject* parent) : QSortFilterProxyModel(parent) {
    setRecursiveFilteringEnabled(true);
  }

  void setFilterText(const QString& text) {
    _text = text.trimmed();
    invalidateFilter();
  }

  void setKindFilter(int group) {
    _group = group;
    invalidateFilter();
  }

  bool filtering() const { return !_text.isEmpty() || _group != AllKinds; }

protected:
  bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override {
    const QAbstractItemModel* model = sourceModel();
    const QModelIndex row = model->index(sourceRow, 0, sourceParent);
    if (!row.isValid()) {
      return false;
    }
    if (_group != AllKinds && row.data(PmiTreeModel::GroupRole).toInt() != _group) {
      return false;
    }
    if (_text.isEmpty()) {
      return true;
    }
    if (!sourceParent.isValid()) {
      // Group row: kept when a child matches (recursive filtering handles that; a
      // group's own title never matches).
      return false;
    }
    return matches(model, sourceRow, sourceParent);
  }

private:
  bool matches(const QAbstractItemModel* model, int sourceRow, const QModelIndex& sourceParent) const {
    for (int column = 0; column < model->columnCount(sourceParent); ++column) {
      const QString text = model->index(sourceRow, column, sourceParent).data().toString();
      if (text.contains(_text, Qt::CaseInsensitive)) {
        return true;
      }
    }
    return false;
  }

  QString _text;
  int _group = AllKinds;
};

PmiPanel::PmiPanel(QWidget* parent) : QWidget(parent) {
  _search = new QLineEdit(this);
  _search->setObjectName(QStringLiteral("pmiFilterEdit"));
  _search->setPlaceholderText(tr("Filter PMI\xE2\x80\xA6"));
  _search->setClearButtonEnabled(true);

  _kind = new QComboBox(this);
  _kind->setObjectName(QStringLiteral("pmiKindCombo"));
  _kind->addItem(tr("All"), AllKinds);
  _kind->addItem(tr("Dimensions"), PmiTreeModel::DimensionsGroup);
  _kind->addItem(tr("Geometric tolerances"), PmiTreeModel::TolerancesGroup);
  _kind->addItem(tr("Datums"), PmiTreeModel::DatumsGroup);
  _kind->addItem(tr("Not interpreted"), PmiTreeModel::NotInterpretedGroup);

  _model = new PmiTreeModel(this);
  _proxy = new PmiFilterProxy(this);
  _proxy->setSourceModel(_model);

  _tree = new QTreeView(this);
  _tree->setObjectName(QStringLiteral("pmiTree"));
  _tree->setModel(_proxy);
  _tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  _tree->setUniformRowHeights(true);
  _tree->setAlternatingRowColors(true);
  _tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  _tree->header()->setStretchLastSection(true);

  _summary = new QLabel(this);
  _summary->setObjectName(QStringLiteral("pmiSummary"));
  _summary->setContentsMargins(4, 2, 4, 2);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(_search);
  layout->addWidget(_kind);
  layout->addWidget(_tree, 1);
  layout->addWidget(_summary);

  connect(_search, &QLineEdit::textChanged, this, &PmiPanel::applyFilter);
  connect(_kind, qOverload<int>(&QComboBox::currentIndexChanged), this, &PmiPanel::applyFilter);
  connect(_tree->selectionModel(), &QItemSelectionModel::selectionChanged, this,
          &PmiPanel::onSelectionChanged);
  connect(_tree, &QAbstractItemView::activated, this, &PmiPanel::onActivated);
  connect(_model, &PmiTreeModel::visibilityRequested, this, &PmiPanel::visibilityRequested);

  updateSummary();
}

void PmiPanel::setModel(std::shared_ptr<const core::PmiModel> model) {
  const bool wasProgrammatic = _programmaticSelection;
  _programmaticSelection = true; // resetting the model is not a user selection
  _model->setModel(std::move(model));
  _programmaticSelection = wasProgrammatic;
  _tree->expandAll();
  updateSummary();
}

void PmiPanel::clear() {
  setModel(nullptr);
}

void PmiPanel::applyFilter() {
  _proxy->setFilterText(_search->text());
  _proxy->setKindFilter(_kind->currentData().toInt());
  if (_proxy->filtering()) {
    _tree->expandAll(); // show the matches, not just the groups
  }
}

void PmiPanel::updateSummary() {
  const std::shared_ptr<const core::PmiModel>& pmi = _model->pmiModel();
  const bool empty = !pmi || pmi->isEmpty();
  _search->setEnabled(!empty);
  _kind->setEnabled(!empty);
  if (!pmi) {
    _summary->clear();
    return;
  }
  if (empty) {
    _summary->setText(tr("No PMI in this file"));
    return;
  }
  const qsizetype items = static_cast<qsizetype>(pmi->items().size());
  const qsizetype views = static_cast<qsizetype>(pmi->views().size());
  QStringList parts;
  if (items > 0) {
    parts << (items == 1 ? tr("%1 annotation") : tr("%1 annotations")).arg(items);
  }
  if (views > 0) {
    parts << (views == 1 ? tr("%1 saved view") : tr("%1 saved views")).arg(views);
  }
  _summary->setText(parts.join(QString::fromUtf8(" \xC2\xB7 ")));
}

void PmiPanel::onSelectionChanged() {
  if (_programmaticSelection) {
    return;
  }
  std::vector<core::PmiId> ids;
  const QModelIndexList rows = _tree->selectionModel()->selectedRows(0);
  ids.reserve(static_cast<size_t>(rows.size()));
  for (const QModelIndex& proxyIndex : rows) {
    const core::PmiId id = _model->pmiId(_proxy->mapToSource(proxyIndex));
    if (id != core::NoPmi) {
      ids.push_back(id);
    }
  }
  Q_EMIT pmiSelected(std::move(ids));
}

void PmiPanel::onActivated(const QModelIndex& proxyIndex) {
  const int viewId = _model->viewId(_proxy->mapToSource(proxyIndex));
  if (viewId >= 0) {
    Q_EMIT viewActivated(viewId);
  }
}

void PmiPanel::selectPmi(const std::vector<core::PmiId>& ids) {
  const bool wasProgrammatic = _programmaticSelection;
  _programmaticSelection = true;

  QItemSelection selection;
  QModelIndex first;
  for (const core::PmiId id : ids) {
    const QModelIndex proxyIndex = _proxy->mapFromSource(_model->indexOf(id));
    if (!proxyIndex.isValid()) {
      continue; // unknown id, or hidden by the filter
    }
    if (!first.isValid()) {
      first = proxyIndex;
    }
    selection.select(proxyIndex, proxyIndex);
  }
  QItemSelectionModel* selectionModel = _tree->selectionModel();
  if (selection.isEmpty()) {
    selectionModel->clearSelection();
  } else {
    selectionModel->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    selectionModel->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
    _tree->expand(first.parent());
    _tree->scrollTo(first);
  }

  _programmaticSelection = wasProgrammatic;
}

void PmiPanel::setVisible(const std::vector<core::PmiId>& ids, bool visible) {
  _model->setVisible(ids, visible);
}

std::vector<core::PmiId> PmiPanel::visibleIds() const {
  return _model->visibleIds();
}

} // namespace sc::app
