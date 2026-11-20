#include "app/MeasurementsPanel.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace sc::app {

namespace {

constexpr int kIdRole = Qt::UserRole + 1;

QString qs(const std::string& text) {
  return QString::fromStdString(text);
}

QToolButton* toolButton(const QString& text, const QString& tip, QWidget* parent) {
  auto* button = new QToolButton(parent);
  button->setText(text);
  button->setToolTip(tip);
  button->setAutoRaise(true);
  return button;
}

// Event filter on the list: Delete (and Backspace, the Mac delete key) removes
// the selected results. A filter rather than a shortcut so it works without
// shortcut-context juggling between docks.
class DeleteKeyFilter : public QObject {
public:
  explicit DeleteKeyFilter(MeasurementsPanel* panel) : QObject(panel), m_panel(panel) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::KeyPress) {
      const auto* key = static_cast<QKeyEvent*>(event);
      if ((key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace) &&
          key->modifiers() == Qt::NoModifier) {
        m_panel->removeSelected();
        return true;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  MeasurementsPanel* m_panel;
};

// Only successful results with witness geometry get a checkbox.
bool hasWitness(const core::Measurement& m) {
  return m.ok && !m.witness.empty();
}

} // namespace

MeasurementsPanel::MeasurementsPanel(QWidget* parent) : QWidget(parent) {
  m_hint = new QLabel(this);
  m_hint->setWordWrap(true);
  m_hint->setContentsMargins(4, 2, 4, 2);

  m_tree = new QTreeWidget(this);
  m_tree->setColumnCount(2);
  m_tree->setHeaderLabels({tr("Measurement"), tr("Value")});
  m_tree->setAlternatingRowColors(true);
  m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
  m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_tree->header()->setStretchLastSection(true);
  m_tree->installEventFilter(new DeleteKeyFilter(this));
  connect(m_tree, &QTreeWidget::currentItemChanged, this, [this] {
    if (m_updating) {
      return;
    }
    updateButtons();
    Q_EMIT currentChanged(currentIndex());
    Q_EMIT displayChanged();
  });
  connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &MeasurementsPanel::updateButtons);
  connect(m_tree, &QTreeWidget::itemChanged, this, &MeasurementsPanel::onItemChanged);
  connect(m_tree, &QWidget::customContextMenuRequested, this, &MeasurementsPanel::showContextMenu);

  m_copy = toolButton(tr("Copy"), tr("Copy the selected results as text"), this);
  m_export = toolButton(tr("Export CSV\xE2\x80\xA6"), tr("Save all results as a CSV file"), this);
  m_showAll = toolButton(tr("Hide All"), tr("Show or hide every result in the 3D view"), this);
  m_remove = toolButton(tr("Remove"), tr("Remove the selected results (Delete)"), this);
  m_clear = toolButton(tr("Clear"), tr("Remove all results"), this);
  connect(m_copy, &QToolButton::clicked, this, &MeasurementsPanel::copySelected);
  connect(m_export, &QToolButton::clicked, this, &MeasurementsPanel::exportCsv);
  connect(m_showAll, &QToolButton::clicked, this, [this] { setAllShown(!anyShown()); });
  connect(m_remove, &QToolButton::clicked, this, &MeasurementsPanel::removeSelected);
  connect(m_clear, &QToolButton::clicked, this, &MeasurementsPanel::clear);

  auto* buttons = new QHBoxLayout();
  buttons->setContentsMargins(0, 0, 0, 0);
  buttons->addWidget(m_copy);
  buttons->addWidget(m_export);
  buttons->addWidget(m_showAll);
  buttons->addStretch();
  buttons->addWidget(m_remove);
  buttons->addWidget(m_clear);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(2);
  layout->addWidget(m_hint);
  layout->addWidget(m_tree);
  layout->addLayout(buttons);
  updateButtons();
}

void MeasurementsPanel::setHint(const QString& text) {
  m_hint->setText(text);
}

void MeasurementsPanel::addMeasurement(const core::Measurement& m) {
  const int id = m_nextId++;
  const bool showable = hasWitness(m);
  m_measurements.push_back(m);
  m_ids.push_back(id);
  m_shown.push_back(showable);

  // Built before it joins the tree, so setting its check state emits nothing.
  auto* item =
      new QTreeWidgetItem({QString::fromUtf8(core::toString(m.mode)), qs(m.ok ? m.headline : m.error)});
  item->setData(0, kIdRole, id);
  item->setToolTip(1, item->text(1));
  if (showable) {
    item->setCheckState(0, Qt::Checked);
    item->setToolTip(0, tr("Show in the 3D view"));
  } else {
    item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
  }
  if (!m.ok) {
    item->setForeground(1, QColor(180, 40, 40));
  }
  for (const std::string& entity : m.entities) {
    auto* child = new QTreeWidgetItem(
        item, {m.mode == core::MeasureMode::MassProperties ? tr("Part") : tr("Picked"), qs(entity)});
    child->setData(0, kIdRole, id);
    child->setToolTip(1, child->text(1));
  }
  for (const auto& [key, value] : m.details) {
    auto* child = new QTreeWidgetItem(item, {qs(key), qs(value)});
    child->setData(0, kIdRole, id);
    child->setToolTip(1, child->text(1));
  }
  m_tree->addTopLevelItem(item);
  // Older results keep whatever the user expanded, so several can be compared.
  item->setExpanded(true);
  m_tree->setCurrentItem(item); // emits currentChanged and displayChanged
  m_tree->scrollToItem(item);
  updateButtons();
}

void MeasurementsPanel::clear() {
  m_updating = true;
  m_tree->clear();
  m_updating = false;
  m_measurements.clear();
  m_ids.clear();
  m_shown.clear();
  updateButtons();
  Q_EMIT currentChanged(-1);
  Q_EMIT displayChanged();
}

int MeasurementsPanel::indexOfId(int id) const {
  const auto it = std::find(m_ids.begin(), m_ids.end(), id);
  return it != m_ids.end() ? static_cast<int>(it - m_ids.begin()) : -1;
}

int MeasurementsPanel::currentIndex() const {
  const QTreeWidgetItem* item = m_tree->currentItem();
  return item != nullptr ? indexOfId(item->data(0, kIdRole).toInt()) : -1;
}

std::vector<int> MeasurementsPanel::selectedIndices() const {
  std::vector<int> indices;
  for (const QTreeWidgetItem* item : m_tree->selectedItems()) {
    const int index = indexOfId(item->data(0, kIdRole).toInt());
    if (index >= 0 && std::find(indices.begin(), indices.end(), index) == indices.end()) {
      indices.push_back(index);
    }
  }
  std::sort(indices.begin(), indices.end());
  return indices;
}

void MeasurementsPanel::setShown(int index, bool shown) {
  if (index < 0 || index >= static_cast<int>(m_measurements.size())) {
    return;
  }
  if (!hasWitness(m_measurements[static_cast<size_t>(index)])) {
    return;
  }
  QTreeWidgetItem* item = m_tree->topLevelItem(index);         // rows and results share their order
  item->setCheckState(0, shown ? Qt::Checked : Qt::Unchecked); // onItemChanged does the rest
}

void MeasurementsPanel::setAllShown(bool shown) {
  bool changed = false;
  m_updating = true;
  for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
    QTreeWidgetItem* item = m_tree->topLevelItem(i);
    if (!hasWitness(m_measurements[static_cast<size_t>(i)]) || m_shown[static_cast<size_t>(i)] == shown) {
      continue;
    }
    item->setCheckState(0, shown ? Qt::Checked : Qt::Unchecked);
    m_shown[static_cast<size_t>(i)] = shown;
    changed = true;
  }
  m_updating = false;
  updateButtons();
  if (changed) {
    Q_EMIT displayChanged();
  }
}

void MeasurementsPanel::onItemChanged(QTreeWidgetItem* item, int column) {
  if (m_updating || column != 0 || item->parent() != nullptr) {
    return;
  }
  const int index = indexOfId(item->data(0, kIdRole).toInt());
  const bool shown = item->checkState(0) == Qt::Checked;
  if (index < 0 || !hasWitness(m_measurements[static_cast<size_t>(index)]) ||
      m_shown[static_cast<size_t>(index)] == shown) {
    return;
  }
  m_shown[static_cast<size_t>(index)] = shown;
  updateButtons();
  Q_EMIT displayChanged();
}

void MeasurementsPanel::copySelected() {
  std::vector<int> indices = selectedIndices();
  if (indices.empty() && currentIndex() >= 0) {
    indices.push_back(currentIndex());
  }
  std::string text;
  for (const int index : indices) {
    text += core::toClipboardText(m_measurements[static_cast<size_t>(index)]);
  }
  QApplication::clipboard()->setText(qs(text));
}

void MeasurementsPanel::exportCsv() {
  const QString path = QFileDialog::getSaveFileName(this, tr("Export Measurements"), tr("measurements.csv"),
                                                    tr("CSV files (*.csv)"));
  if (path.isEmpty()) {
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::warning(this, tr("Export Measurements"),
                         tr("Could not write %1:\n%2").arg(path, file.errorString()));
    return;
  }
  // UTF-8 with BOM so spreadsheet apps show units (mm², °) correctly.
  file.write("\xEF\xBB\xBF");
  file.write(QByteArray::fromStdString(core::toCsv(m_measurements)));
}

void MeasurementsPanel::removeSelected() {
  std::vector<int> remove = selectedIndices();
  if (remove.empty() && currentIndex() >= 0) {
    remove.push_back(currentIndex());
  }
  if (remove.empty()) {
    return;
  }
  // Remove just those rows, back to front, so the others keep their expansion
  // and the list keeps its scroll position.
  m_updating = true;
  for (auto it = remove.rbegin(); it != remove.rend(); ++it) {
    const int index = *it;
    delete m_tree->takeTopLevelItem(index);
    m_measurements.erase(m_measurements.begin() + index);
    m_ids.erase(m_ids.begin() + index);
    m_shown.erase(m_shown.begin() + index);
  }
  // The row that took the first removed one's place becomes current, so
  // pressing Delete repeatedly walks down the list.
  const int next = std::min(remove.front(), m_tree->topLevelItemCount() - 1);
  m_tree->clearSelection();
  if (next >= 0) {
    m_tree->setCurrentItem(m_tree->topLevelItem(next));
  }
  m_updating = false;
  updateButtons();
  Q_EMIT currentChanged(currentIndex());
  Q_EMIT displayChanged();
}

void MeasurementsPanel::showContextMenu(const QPoint& pos) {
  if (QTreeWidgetItem* item = m_tree->itemAt(pos); item != nullptr && !item->isSelected()) {
    m_tree->setCurrentItem(item);
  }
  std::vector<int> selected = selectedIndices();
  if (selected.empty() && currentIndex() >= 0) {
    selected.push_back(currentIndex());
  }
  bool anySelectedShowable = false;
  bool allSelectedShown = true;
  for (const int index : selected) {
    if (hasWitness(m_measurements[static_cast<size_t>(index)])) {
      anySelectedShowable = true;
      allSelectedShown = allSelectedShown && m_shown[static_cast<size_t>(index)];
    }
  }

  QMenu menu(this);
  QAction* copy = menu.addAction(tr("Copy"), this, &MeasurementsPanel::copySelected);
  copy->setEnabled(!selected.empty());
  QAction* remove = menu.addAction(tr("Remove"), this, &MeasurementsPanel::removeSelected);
  remove->setShortcut(QKeySequence::Delete); // shown as a hint; the key is handled by the list
  remove->setEnabled(!selected.empty());
  menu.addSeparator();
  QAction* show = menu.addAction(tr("Show in 3D View"));
  show->setCheckable(true);
  show->setChecked(anySelectedShowable && allSelectedShown);
  show->setEnabled(anySelectedShowable);
  connect(show, &QAction::toggled, this, [this, selected](bool on) {
    for (const int index : selected) {
      setShown(index, on);
    }
  });
  menu.addAction(tr("Show All"), this, [this] { setAllShown(true); })->setEnabled(anyShowable());
  menu.addAction(tr("Hide All"), this, [this] { setAllShown(false); })->setEnabled(anyShown());
  menu.addSeparator();
  menu.addAction(tr("Clear All"), this, &MeasurementsPanel::clear)->setEnabled(!m_measurements.empty());
  menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

bool MeasurementsPanel::anyShowable() const {
  return std::any_of(m_measurements.begin(), m_measurements.end(), hasWitness);
}

bool MeasurementsPanel::anyShown() const {
  return std::find(m_shown.begin(), m_shown.end(), true) != m_shown.end();
}

void MeasurementsPanel::updateButtons() {
  const bool any = !m_measurements.empty();
  m_copy->setEnabled(any);
  m_export->setEnabled(any);
  m_clear->setEnabled(any);
  m_remove->setEnabled(!m_tree->selectedItems().isEmpty() || m_tree->currentItem() != nullptr);
  m_showAll->setEnabled(anyShowable());
  m_showAll->setText(anyShown() ? tr("Hide All") : tr("Show All"));
}

} // namespace sc::app
