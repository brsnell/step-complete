#include "app/MeasurementsPanel.h"

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace sc::app {

namespace {

constexpr int kIndexRole = Qt::UserRole + 1;

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
  m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_tree->header()->setStretchLastSection(true);
  connect(m_tree, &QTreeWidget::currentItemChanged, this, [this] {
    updateButtons();
    Q_EMIT currentChanged(currentIndex());
  });
  connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &MeasurementsPanel::updateButtons);

  m_copy = toolButton(tr("Copy"), tr("Copy the selected results as text"), this);
  m_export = toolButton(tr("Export CSV\xE2\x80\xA6"), tr("Save all results as a CSV file"), this);
  m_remove = toolButton(tr("Remove"), tr("Remove the selected results"), this);
  m_clear = toolButton(tr("Clear"), tr("Remove all results"), this);
  connect(m_copy, &QToolButton::clicked, this, &MeasurementsPanel::copySelected);
  connect(m_export, &QToolButton::clicked, this, &MeasurementsPanel::exportCsv);
  connect(m_remove, &QToolButton::clicked, this, &MeasurementsPanel::removeSelected);
  connect(m_clear, &QToolButton::clicked, this, &MeasurementsPanel::clear);

  auto* buttons = new QHBoxLayout();
  buttons->setContentsMargins(0, 0, 0, 0);
  buttons->addWidget(m_copy);
  buttons->addWidget(m_export);
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
  const int index = static_cast<int>(m_measurements.size());
  m_measurements.push_back(m);

  auto* item = new QTreeWidgetItem(
      m_tree, {QString::fromUtf8(core::toString(m.mode)), qs(m.ok ? m.headline : m.error)});
  item->setData(0, kIndexRole, index);
  item->setToolTip(1, item->text(1));
  if (!m.ok) {
    item->setForeground(1, QColor(180, 40, 40));
  }
  for (const std::string& entity : m.entities) {
    auto* child = new QTreeWidgetItem(item, {tr("Picked"), qs(entity)});
    child->setData(0, kIndexRole, index);
    child->setToolTip(1, child->text(1));
  }
  for (const auto& [key, value] : m.details) {
    auto* child = new QTreeWidgetItem(item, {qs(key), qs(value)});
    child->setData(0, kIndexRole, index);
    child->setToolTip(1, child->text(1));
  }
  // Collapse older results; show the new one.
  for (int i = 0; i < m_tree->topLevelItemCount() - 1; ++i) {
    m_tree->topLevelItem(i)->setExpanded(false);
  }
  item->setExpanded(true);
  m_tree->setCurrentItem(item);
  m_tree->scrollToItem(item);
  updateButtons();
}

void MeasurementsPanel::clear() {
  m_tree->clear();
  m_measurements.clear();
  updateButtons();
  Q_EMIT currentChanged(-1);
}

int MeasurementsPanel::currentIndex() const {
  const QTreeWidgetItem* item = m_tree->currentItem();
  return item != nullptr ? item->data(0, kIndexRole).toInt() : -1;
}

void MeasurementsPanel::copySelected() {
  std::vector<int> indices;
  for (const QTreeWidgetItem* item : m_tree->selectedItems()) {
    const int index = item->data(0, kIndexRole).toInt();
    if (std::find(indices.begin(), indices.end(), index) == indices.end()) {
      indices.push_back(index);
    }
  }
  if (indices.empty() && currentIndex() >= 0) {
    indices.push_back(currentIndex());
  }
  std::sort(indices.begin(), indices.end());
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
  std::vector<int> remove;
  for (const QTreeWidgetItem* item : m_tree->selectedItems()) {
    remove.push_back(item->data(0, kIndexRole).toInt());
  }
  if (remove.empty()) {
    return;
  }
  std::vector<core::Measurement> kept;
  for (size_t i = 0; i < m_measurements.size(); ++i) {
    if (std::find(remove.begin(), remove.end(), static_cast<int>(i)) == remove.end()) {
      kept.push_back(std::move(m_measurements[i]));
    }
  }
  clear();
  for (const core::Measurement& m : kept) {
    addMeasurement(m);
  }
}

void MeasurementsPanel::updateButtons() {
  const bool any = !m_measurements.empty();
  m_copy->setEnabled(any);
  m_export->setEnabled(any);
  m_clear->setEnabled(any);
  m_remove->setEnabled(!m_tree->selectedItems().isEmpty());
}

} // namespace sc::app
