#include "app/ClashPanel.h"

#include "core/Units.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QStringList>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace sc::app {

namespace {

constexpr int IndexRole = Qt::UserRole + 1;

QString qs(const std::string& text) {
  return QString::fromStdString(text);
}

QString counted(int count, const char* one, const char* many) {
  return QStringLiteral("%1 %2").arg(count).arg(QLatin1String(count == 1 ? one : many));
}

QString idleText() {
  return ClashPanel::tr("Measure > Check Clashes checks the selected parts against each other, or every "
                        "visible part.");
}

} // namespace

ClashPanel::ClashPanel(QWidget* parent) : QWidget(parent) {
  _summary = new QLabel(idleText(), this);
  _summary->setObjectName(QStringLiteral("clashSummary"));
  _summary->setWordWrap(true);
  _summary->setContentsMargins(4, 2, 4, 2);

  _progress = new QProgressBar(this);
  _progress->setRange(0, 100);
  _progress->setVisible(false);
  _cancel = new QToolButton(this);
  _cancel->setText(tr("Cancel"));
  _cancel->setAutoRaise(true);
  _cancel->setVisible(false);
  _rerun = new QToolButton(this);
  _rerun->setText(tr("Check Again"));
  _rerun->setToolTip(tr("Check the same parts again with the same options"));
  _rerun->setAutoRaise(true);
  _rerun->setEnabled(false);

  _tree = new QTreeWidget(this);
  _tree->setObjectName(QStringLiteral("clashList"));
  _tree->setColumnCount(4);
  _tree->setHeaderLabels({tr("Part"), tr("Other part"), tr("Kind"), tr("Overlap or gap")});
  _tree->setRootIsDecorated(false);
  _tree->setUniformRowHeights(true);
  _tree->setSelectionMode(QAbstractItemView::SingleSelection);
  _tree->header()->setStretchLastSection(true);

  auto* bar = new QHBoxLayout;
  bar->setContentsMargins(0, 0, 0, 0);
  bar->addWidget(_progress, 1);
  bar->addWidget(_cancel);
  bar->addStretch();
  bar->addWidget(_rerun);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(2, 2, 2, 2);
  layout->addWidget(_summary);
  layout->addLayout(bar);
  layout->addWidget(_tree, 1);

  connect(_cancel, &QToolButton::clicked, this, &ClashPanel::cancelRequested);
  connect(_rerun, &QToolButton::clicked, this, &ClashPanel::rerunRequested);
  connect(_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
    Q_EMIT clashChosen(item == nullptr ? -1 : item->data(0, IndexRole).toInt());
  });
  connect(_tree, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem* item) { Q_EMIT clashActivated(item->data(0, IndexRole).toInt()); });
}

QString ClashPanel::summaryOf(const core::ClashResult& result) {
  if (result.cancelled) {
    return tr("Clash check cancelled.");
  }
  const QString scope = tr("among %1 (%2 checked).")
                            .arg(counted(result.partsChecked, "part", "parts"))
                            .arg(counted(result.pairsChecked, "pair", "pairs"));
  if (result.clashCount == 0) {
    return tr("No clashes") + QLatin1Char(' ') + scope;
  }
  QStringList kinds;
  if (result.interferences > 0) {
    kinds << counted(result.interferences, "interference", "interferences");
  }
  if (result.touching > 0) {
    kinds << counted(result.touching, "touching pair", "touching pairs");
  }
  if (result.clearances > 0) {
    kinds << counted(result.clearances, "pair too close", "pairs too close");
  }
  if (result.failed > 0) {
    kinds << counted(result.failed, "pair not checked", "pairs not checked");
  }
  return kinds.join(QStringLiteral(", ")) + QLatin1Char(' ') + scope;
}

void ClashPanel::setRunning(int percent, const QString& stage) {
  if (!_running) {
    _tree->clear();
    _clashes.clear();
  }
  _running = true;
  _summary->setText(stage.isEmpty() ? tr("Checking clashes…") : stage + QStringLiteral("…"));
  _progress->setValue(percent);
  _progress->setVisible(true);
  _cancel->setVisible(true);
  _rerun->setEnabled(false);
}

void ClashPanel::setResult(const core::ClashResult& result, const core::AssemblyIndex& index,
                           const core::MeasureDisplayOptions& units) {
  _running = false;
  _progress->setVisible(false);
  _cancel->setVisible(false);
  _rerun->setEnabled(true);
  _tree->clear();
  _clashes = result.clashes;

  QString text = summaryOf(result);
  if (static_cast<int>(_clashes.size()) < result.clashCount) {
    text += QLatin1Char(' ') + tr("Only the first %1 are listed.").arg(core::MaxClashRows);
  }
  _summary->setText(text);

  QList<QTreeWidgetItem*> rows;
  rows.reserve(static_cast<qsizetype>(_clashes.size()));
  for (size_t i = 0; i < _clashes.size(); ++i) {
    const core::Clash& clash = _clashes[i];
    QString value;
    switch (clash.kind) {
    case core::ClashKind::Interference:
      value = qs(core::formatVolume(clash.volume, units.lengthUnit, units.decimals));
      break;
    case core::ClashKind::Clearance:
      value = tr("%1 gap").arg(qs(core::formatLength(clash.distance, units.lengthUnit, units.decimals)));
      break;
    case core::ClashKind::Touching:
    case core::ClashKind::Failed:
      break;
    }
    auto* item = new QTreeWidgetItem({qs(index.node(clash.a).name), qs(index.node(clash.b).name),
                                      QString::fromLatin1(core::toString(clash.kind)), value});
    item->setData(0, IndexRole, static_cast<int>(i));
    item->setToolTip(0, qs(index.displayPath(clash.a)));
    item->setToolTip(1, qs(index.displayPath(clash.b)));
    rows.append(item);
  }
  _tree->addTopLevelItems(rows);
  for (int column = 0; column < 3; ++column) {
    _tree->resizeColumnToContents(column);
  }
}

void ClashPanel::clear() {
  _running = false;
  _progress->setVisible(false);
  _cancel->setVisible(false);
  _rerun->setEnabled(false);
  _tree->clear();
  _clashes.clear();
  _summary->setText(idleText());
}

QString ClashPanel::summary() const {
  return _summary->text();
}

int ClashPanel::rowCount() const {
  return _tree->topLevelItemCount();
}

int ClashPanel::currentIndex() const {
  const QTreeWidgetItem* item = _tree->currentItem();
  return item == nullptr ? -1 : item->data(0, IndexRole).toInt();
}

void ClashPanel::setCurrentIndex(int index) {
  _tree->setCurrentItem(index >= 0 && index < rowCount() ? _tree->topLevelItem(index) : nullptr);
}

} // namespace sc::app
