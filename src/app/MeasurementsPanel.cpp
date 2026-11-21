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
#include <numeric>

namespace sc::app {

namespace {

constexpr int IdRole = Qt::UserRole + 1;

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
  explicit DeleteKeyFilter(MeasurementsPanel* panel) : QObject(panel), _panel(panel) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::KeyPress) {
      const auto* key = static_cast<QKeyEvent*>(event);
      // Backspace is the delete key on Mac keyboards only; numpad Delete carries KeypadModifier.
#ifdef Q_OS_MACOS
      const bool deleteKey = key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace;
#else
      const bool deleteKey = key->key() == Qt::Key_Delete;
#endif
      if (deleteKey && (key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
        _panel->removeSelected();
        return true;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  MeasurementsPanel* _panel;
};

// Only successful results with witness geometry get a checkbox.
bool hasWitness(const core::Measurement& measurement) {
  return measurement.ok && !measurement.witness.empty();
}

} // namespace

MeasurementsPanel::MeasurementsPanel(QWidget* parent) : QWidget(parent) {
  _hint = new QLabel(this);
  _hint->setWordWrap(true);
  _hint->setContentsMargins(4, 2, 4, 2);

  _tree = new QTreeWidget(this);
  _tree->setColumnCount(2);
  _tree->setHeaderLabels({tr("Measurement"), tr("Value")});
  _tree->setAlternatingRowColors(true);
  _tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  _tree->setContextMenuPolicy(Qt::CustomContextMenu);
  _tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  _tree->header()->setStretchLastSection(true);
  _tree->installEventFilter(new DeleteKeyFilter(this));
  connect(_tree, &QTreeWidget::currentItemChanged, this, [this] {
    if (_updating) {
      return;
    }
    updateButtons();
    Q_EMIT currentChanged(currentIndex());
    Q_EMIT displayChanged();
  });
  connect(_tree, &QTreeWidget::itemSelectionChanged, this, &MeasurementsPanel::updateButtons);
  connect(_tree, &QTreeWidget::itemChanged, this, &MeasurementsPanel::onItemChanged);
  connect(_tree, &QWidget::customContextMenuRequested, this, &MeasurementsPanel::showContextMenu);

  _copy = toolButton(tr("Copy"), tr("Copy the selected results as text"), this);
  _export = toolButton(tr("Export CSV\xE2\x80\xA6"), tr("Save all results as a CSV file"), this);
  _showAll = toolButton(tr("Hide All"), tr("Show or hide every result in the 3D view"), this);
  _remove = toolButton(tr("Remove"), tr("Remove the selected results (Delete)"), this);
  _clear = toolButton(tr("Clear"), tr("Remove all results"), this);
  connect(_copy, &QToolButton::clicked, this, &MeasurementsPanel::copySelected);
  connect(_export, &QToolButton::clicked, this, &MeasurementsPanel::exportCsv);
  connect(_showAll, &QToolButton::clicked, this, [this] { setAllShown(!anyShown()); });
  connect(_remove, &QToolButton::clicked, this, &MeasurementsPanel::removeSelected);
  connect(_clear, &QToolButton::clicked, this, &MeasurementsPanel::clear);

  auto* buttons = new QHBoxLayout();
  buttons->setContentsMargins(0, 0, 0, 0);
  buttons->addWidget(_copy);
  buttons->addWidget(_export);
  buttons->addWidget(_showAll);
  buttons->addStretch();
  buttons->addWidget(_remove);
  buttons->addWidget(_clear);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(2);
  layout->addWidget(_hint);
  layout->addWidget(_tree);
  layout->addLayout(buttons);
  updateButtons();
}

void MeasurementsPanel::setHint(const QString& text) {
  _hint->setText(text);
}

void MeasurementsPanel::addMeasurement(const core::Measurement& measurement) {
  const int id = _nextId++;
  const bool showable = hasWitness(measurement);
  _measurements.push_back(measurement);
  _ids.push_back(id);
  _shown.push_back(showable);

  // Built before it joins the tree, so setting its check state emits nothing.
  auto* item = new QTreeWidgetItem({QString::fromUtf8(core::toString(measurement.mode)),
                                    qs(measurement.ok ? measurement.headline : measurement.error)});
  item->setData(0, IdRole, id);
  item->setToolTip(1, item->text(1));
  if (showable) {
    item->setCheckState(0, Qt::Checked);
    item->setToolTip(0, tr("Show in the 3D view"));
  } else {
    item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
  }
  if (!measurement.ok) {
    item->setForeground(1, QColor(180, 40, 40));
  }
  for (const std::string& entity : measurement.entities) {
    auto* child = new QTreeWidgetItem(
        item,
        {measurement.mode == core::MeasureMode::MassProperties ? tr("Part") : tr("Picked"), qs(entity)});
    child->setData(0, IdRole, id);
    child->setToolTip(1, child->text(1));
  }
  for (const auto& [key, value] : measurement.details) {
    auto* child = new QTreeWidgetItem(item, {qs(key), qs(value)});
    child->setData(0, IdRole, id);
    child->setToolTip(1, child->text(1));
  }
  _tree->addTopLevelItem(item);
  // Older results keep whatever the user expanded, so several can be compared.
  item->setExpanded(true);
  _tree->setCurrentItem(item); // emits currentChanged and displayChanged
  _tree->scrollToItem(item);
  updateButtons();
}

void MeasurementsPanel::clear() {
  _updating = true;
  _tree->clear();
  _updating = false;
  _measurements.clear();
  _ids.clear();
  _shown.clear();
  updateButtons();
  Q_EMIT currentChanged(-1);
  Q_EMIT displayChanged();
}

int MeasurementsPanel::indexOfId(int id) const {
  const auto it = std::find(_ids.begin(), _ids.end(), id);
  return it != _ids.end() ? static_cast<int>(it - _ids.begin()) : -1;
}

int MeasurementsPanel::currentIndex() const {
  const QTreeWidgetItem* item = _tree->currentItem();
  return item != nullptr ? indexOfId(item->data(0, IdRole).toInt()) : -1;
}

std::vector<int> MeasurementsPanel::selectedIndices() const {
  std::vector<int> indices;
  for (const QTreeWidgetItem* item : _tree->selectedItems()) {
    const int index = indexOfId(item->data(0, IdRole).toInt());
    if (index >= 0 && std::find(indices.begin(), indices.end(), index) == indices.end()) {
      indices.push_back(index);
    }
  }
  std::sort(indices.begin(), indices.end());
  return indices;
}

void MeasurementsPanel::setShown(int index, bool shown) {
  if (index < 0 || index >= static_cast<int>(_measurements.size())) {
    return;
  }
  if (!hasWitness(_measurements[static_cast<size_t>(index)])) {
    return;
  }
  QTreeWidgetItem* item = _tree->topLevelItem(index);          // rows and results share their order
  item->setCheckState(0, shown ? Qt::Checked : Qt::Unchecked); // onItemChanged does the rest
}

void MeasurementsPanel::setAllShown(bool shown) {
  std::vector<int> all(_measurements.size());
  std::iota(all.begin(), all.end(), 0);
  setShownRows(all, shown);
}

// Checks or unchecks several results with one displayChanged, so the 3D view syncs once.
void MeasurementsPanel::setShownRows(const std::vector<int>& indices, bool shown) {
  bool changed = false;
  _updating = true;
  for (const int i : indices) {
    if (i < 0 || i >= static_cast<int>(_measurements.size())) {
      continue;
    }
    QTreeWidgetItem* item = _tree->topLevelItem(i);
    if (!hasWitness(_measurements[static_cast<size_t>(i)]) || _shown[static_cast<size_t>(i)] == shown) {
      continue;
    }
    item->setCheckState(0, shown ? Qt::Checked : Qt::Unchecked);
    _shown[static_cast<size_t>(i)] = shown;
    changed = true;
  }
  _updating = false;
  updateButtons();
  if (changed) {
    Q_EMIT displayChanged();
  }
}

void MeasurementsPanel::onItemChanged(QTreeWidgetItem* item, int column) {
  if (_updating || column != 0 || item->parent() != nullptr) {
    return;
  }
  const int index = indexOfId(item->data(0, IdRole).toInt());
  const bool shown = item->checkState(0) == Qt::Checked;
  if (index < 0 || !hasWitness(_measurements[static_cast<size_t>(index)]) ||
      _shown[static_cast<size_t>(index)] == shown) {
    return;
  }
  _shown[static_cast<size_t>(index)] = shown;
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
    text += core::toClipboardText(_measurements[static_cast<size_t>(index)]);
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
  file.write(QByteArray::fromStdString(core::toCsv(_measurements)));
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
  _updating = true;
  for (auto it = remove.rbegin(); it != remove.rend(); ++it) {
    const int index = *it;
    delete _tree->takeTopLevelItem(index);
    _measurements.erase(_measurements.begin() + index);
    _ids.erase(_ids.begin() + index);
    _shown.erase(_shown.begin() + index);
  }
  // The row that took the first removed one's place becomes current, so
  // pressing Delete repeatedly walks down the list.
  const int next = std::min(remove.front(), _tree->topLevelItemCount() - 1);
  _tree->clearSelection();
  if (next >= 0) {
    _tree->setCurrentItem(_tree->topLevelItem(next));
  }
  _updating = false;
  updateButtons();
  Q_EMIT currentChanged(currentIndex());
  Q_EMIT displayChanged();
}

void MeasurementsPanel::showContextMenu(const QPoint& pos) {
  // Right-clicking an unselected row acts on that row alone, as in file managers.
  if (QTreeWidgetItem* item = _tree->itemAt(pos); item != nullptr && !item->isSelected()) {
    _tree->clearSelection();
    _tree->setCurrentItem(item);
  }
  std::vector<int> selected = selectedIndices();
  if (selected.empty() && currentIndex() >= 0) {
    selected.push_back(currentIndex());
  }
  bool anySelectedShowable = false;
  bool allSelectedShown = true;
  for (const int index : selected) {
    if (hasWitness(_measurements[static_cast<size_t>(index)])) {
      anySelectedShowable = true;
      allSelectedShown = allSelectedShown && _shown[static_cast<size_t>(index)];
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
  connect(show, &QAction::toggled, this, [this, selected](bool on) { setShownRows(selected, on); });
  menu.addAction(tr("Show All"), this, [this] { setAllShown(true); })->setEnabled(anyShowable());
  menu.addAction(tr("Hide All"), this, [this] { setAllShown(false); })->setEnabled(anyShown());
  menu.addSeparator();
  menu.addAction(tr("Clear All"), this, &MeasurementsPanel::clear)->setEnabled(!_measurements.empty());
  menu.exec(_tree->viewport()->mapToGlobal(pos));
}

bool MeasurementsPanel::anyShowable() const {
  return std::any_of(_measurements.begin(), _measurements.end(), hasWitness);
}

bool MeasurementsPanel::anyShown() const {
  return std::find(_shown.begin(), _shown.end(), true) != _shown.end();
}

void MeasurementsPanel::updateButtons() {
  const bool any = !_measurements.empty();
  _copy->setEnabled(any);
  _export->setEnabled(any);
  _clear->setEnabled(any);
  _remove->setEnabled(!_tree->selectedItems().isEmpty() || _tree->currentItem() != nullptr);
  _showAll->setEnabled(anyShowable());
  _showAll->setText(anyShown() ? tr("Hide All") : tr("Show All"));
}

} // namespace sc::app
