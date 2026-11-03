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

constexpr int kAllKinds = -1;

} // namespace

// Filters by group (kind filter) and by name/text (case-insensitive). A group row is
// kept when its kind is allowed and, if a text filter is set, when a child matches.
class PmiFilterProxy : public QSortFilterProxyModel {
public:
  explicit PmiFilterProxy(QObject* parent) : QSortFilterProxyModel(parent) {
    setRecursiveFilteringEnabled(true);
  }

  void setFilterText(const QString& text) {
    m_text = text.trimmed();
    invalidateFilter();
  }

  void setKindFilter(int group) {
    m_group = group;
    invalidateFilter();
  }

  bool filtering() const { return !m_text.isEmpty() || m_group != kAllKinds; }

protected:
  bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override {
    const QAbstractItemModel* model = sourceModel();
    const QModelIndex row = model->index(sourceRow, 0, sourceParent);
    if (!row.isValid()) {
      return false;
    }
    if (m_group != kAllKinds && row.data(PmiTreeModel::GroupRole).toInt() != m_group) {
      return false;
    }
    if (m_text.isEmpty()) {
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
      if (text.contains(m_text, Qt::CaseInsensitive)) {
        return true;
      }
    }
    return false;
  }

  QString m_text;
  int m_group = kAllKinds;
};

PmiPanel::PmiPanel(QWidget* parent) : QWidget(parent) {
  m_search = new QLineEdit(this);
  m_search->setObjectName(QStringLiteral("pmiFilterEdit"));
  m_search->setPlaceholderText(tr("Filter PMI\xE2\x80\xA6"));
  m_search->setClearButtonEnabled(true);

  m_kind = new QComboBox(this);
  m_kind->setObjectName(QStringLiteral("pmiKindCombo"));
  m_kind->addItem(tr("All"), kAllKinds);
  m_kind->addItem(tr("Dimensions"), PmiTreeModel::DimensionsGroup);
  m_kind->addItem(tr("Geometric tolerances"), PmiTreeModel::TolerancesGroup);
  m_kind->addItem(tr("Datums"), PmiTreeModel::DatumsGroup);
  m_kind->addItem(tr("Not interpreted"), PmiTreeModel::NotInterpretedGroup);

  m_model = new PmiTreeModel(this);
  m_proxy = new PmiFilterProxy(this);
  m_proxy->setSourceModel(m_model);

  m_tree = new QTreeView(this);
  m_tree->setObjectName(QStringLiteral("pmiTree"));
  m_tree->setModel(m_proxy);
  m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_tree->setUniformRowHeights(true);
  m_tree->setAlternatingRowColors(true);
  m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_tree->header()->setStretchLastSection(true);

  m_summary = new QLabel(this);
  m_summary->setObjectName(QStringLiteral("pmiSummary"));
  m_summary->setContentsMargins(4, 2, 4, 2);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_search);
  layout->addWidget(m_kind);
  layout->addWidget(m_tree, 1);
  layout->addWidget(m_summary);

  connect(m_search, &QLineEdit::textChanged, this, &PmiPanel::applyFilter);
  connect(m_kind, qOverload<int>(&QComboBox::currentIndexChanged), this, &PmiPanel::applyFilter);
  connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged, this,
          &PmiPanel::onSelectionChanged);
  connect(m_tree, &QAbstractItemView::activated, this, &PmiPanel::onActivated);
  connect(m_model, &PmiTreeModel::visibilityRequested, this, &PmiPanel::visibilityRequested);

  updateSummary();
}

void PmiPanel::setModel(std::shared_ptr<const core::PmiModel> model) {
  const bool wasProgrammatic = m_programmaticSelection;
  m_programmaticSelection = true; // resetting the model is not a user selection
  m_model->setModel(std::move(model));
  m_programmaticSelection = wasProgrammatic;
  m_tree->expandAll();
  updateSummary();
}

void PmiPanel::clear() {
  setModel(nullptr);
}

void PmiPanel::applyFilter() {
  m_proxy->setFilterText(m_search->text());
  m_proxy->setKindFilter(m_kind->currentData().toInt());
  if (m_proxy->filtering()) {
    m_tree->expandAll(); // show the matches, not just the groups
  }
}

void PmiPanel::updateSummary() {
  const std::shared_ptr<const core::PmiModel>& pmi = m_model->pmiModel();
  const bool empty = !pmi || pmi->isEmpty();
  m_search->setEnabled(!empty);
  m_kind->setEnabled(!empty);
  if (!pmi) {
    m_summary->clear();
    return;
  }
  if (empty) {
    m_summary->setText(tr("No PMI in this file"));
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
  m_summary->setText(parts.join(QString::fromUtf8(" \xC2\xB7 ")));
}

void PmiPanel::onSelectionChanged() {
  if (m_programmaticSelection) {
    return;
  }
  std::vector<core::PmiId> ids;
  const QModelIndexList rows = m_tree->selectionModel()->selectedRows(0);
  ids.reserve(static_cast<size_t>(rows.size()));
  for (const QModelIndex& proxyIndex : rows) {
    const core::PmiId id = m_model->pmiId(m_proxy->mapToSource(proxyIndex));
    if (id != core::kNoPmi) {
      ids.push_back(id);
    }
  }
  Q_EMIT pmiSelected(std::move(ids));
}

void PmiPanel::onActivated(const QModelIndex& proxyIndex) {
  const int viewId = m_model->viewId(m_proxy->mapToSource(proxyIndex));
  if (viewId >= 0) {
    Q_EMIT viewActivated(viewId);
  }
}

void PmiPanel::selectPmi(const std::vector<core::PmiId>& ids) {
  const bool wasProgrammatic = m_programmaticSelection;
  m_programmaticSelection = true;

  QItemSelection selection;
  QModelIndex first;
  for (const core::PmiId id : ids) {
    const QModelIndex proxyIndex = m_proxy->mapFromSource(m_model->indexOf(id));
    if (!proxyIndex.isValid()) {
      continue; // unknown id, or hidden by the filter
    }
    if (!first.isValid()) {
      first = proxyIndex;
    }
    selection.select(proxyIndex, proxyIndex);
  }
  QItemSelectionModel* selectionModel = m_tree->selectionModel();
  if (selection.isEmpty()) {
    selectionModel->clearSelection();
  } else {
    selectionModel->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    selectionModel->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
    m_tree->expand(first.parent());
    m_tree->scrollTo(first);
  }

  m_programmaticSelection = wasProgrammatic;
}

void PmiPanel::setVisible(const std::vector<core::PmiId>& ids, bool visible) {
  m_model->setVisible(ids, visible);
}

std::vector<core::PmiId> PmiPanel::visibleIds() const {
  return m_model->visibleIds();
}

} // namespace sc::app
