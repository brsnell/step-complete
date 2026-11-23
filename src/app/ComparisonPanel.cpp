#include "app/ComparisonPanel.h"

#include "core/Units.h"

#include <QCheckBox>
#include <QColor>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QStringList>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>

namespace sc::app {

namespace {

constexpr int ChangeRole = Qt::UserRole + 1;

QString idleText() {
  return ComparisonPanel::tr(
      "File > Compare With opens another revision of this model and shows what changed.");
}

QString counted(int count, const char* word) {
  return QStringLiteral("%1 %2").arg(count).arg(QLatin1String(word));
}

// The swatch of a kind of change, as the 3D view colors it.
QColor swatchOf(core::ChangeKind kind) {
  switch (kind) {
  case core::ChangeKind::Unchanged:
    return QColor(199, 199, 199);
  case core::ChangeKind::Moved:
    return QColor(158, 89, 217);
  case core::ChangeKind::Changed:
    return QColor(51, 204, 51);
  case core::ChangeKind::Added:
    return QColor(255, 140, 0);
  case core::ChangeKind::Removed:
    return QColor(230, 26, 26);
  }
  return QColor(Qt::white);
}

} // namespace

ComparisonPanel::ComparisonPanel(QWidget* parent) : QWidget(parent) {
  _summary = new QLabel(idleText(), this);
  _summary->setObjectName(QStringLiteral("comparisonSummary"));
  _summary->setWordWrap(true);
  _summary->setContentsMargins(4, 2, 4, 2);

  _progress = new QProgressBar(this);
  _progress->setRange(0, 100);
  _progress->setVisible(false);
  _cancel = new QToolButton(this);
  _cancel->setText(tr("Cancel"));
  _cancel->setAutoRaise(true);
  _cancel->setVisible(false);

  _tolerance = new QDoubleSpinBox(this);
  _tolerance->setObjectName(QStringLiteral("comparisonTolerance"));
  _tolerance->setRange(0.0001, 1000.0);
  _tolerance->setDecimals(4);
  _tolerance->setValue(0.01);
  _tolerance->setSuffix(QStringLiteral(" mm"));
  _tolerance->setToolTip(tr("Positions and deviations within this are equal (green)"));
  _again = new QToolButton(this);
  _again->setText(tr("Compare Again"));
  _again->setAutoRaise(true);
  _again->setEnabled(false);
  _showUnchanged = new QCheckBox(tr("Show unchanged"), this);
  _showUnchanged->setObjectName(QStringLiteral("comparisonShowUnchanged"));
  _end = new QToolButton(this);
  _end->setText(tr("End Comparison"));
  _end->setAutoRaise(true);
  _end->setEnabled(false);

  _tree = new QTreeWidget(this);
  _tree->setObjectName(QStringLiteral("comparisonList"));
  _tree->setColumnCount(3);
  _tree->setHeaderLabels({tr("Part"), tr("Change"), tr("Largest deviation")});
  _tree->setRootIsDecorated(false);
  _tree->setUniformRowHeights(true);
  _tree->setSelectionMode(QAbstractItemView::SingleSelection);
  _tree->header()->setStretchLastSection(true);

  auto* progressRow = new QHBoxLayout;
  progressRow->setContentsMargins(0, 0, 0, 0);
  progressRow->addWidget(_progress, 1);
  progressRow->addWidget(_cancel);
  auto* options = new QHBoxLayout;
  options->setContentsMargins(4, 0, 4, 0);
  options->addWidget(new QLabel(tr("Tolerance:"), this));
  options->addWidget(_tolerance);
  options->addWidget(_again);
  options->addStretch();
  options->addWidget(_showUnchanged);
  options->addWidget(_end);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(2, 2, 2, 2);
  layout->addWidget(_summary);
  layout->addLayout(progressRow);
  layout->addLayout(options);
  layout->addWidget(_tree, 1);

  connect(_cancel, &QToolButton::clicked, this, &ComparisonPanel::cancelRequested);
  connect(_end, &QToolButton::clicked, this, &ComparisonPanel::endRequested);
  connect(_again, &QToolButton::clicked, this, [this] { Q_EMIT compareAgainRequested(tolerance()); });
  connect(_showUnchanged, &QCheckBox::toggled, this, [this] { fillRows(); });
  connect(_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
    Q_EMIT changeChosen(item == nullptr ? -1 : item->data(0, ChangeRole).toInt());
  });
  connect(_tree, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem* item) { Q_EMIT changeActivated(item->data(0, ChangeRole).toInt()); });
}

QString ComparisonPanel::summaryOf(const core::ComparisonResult& result, const QString& otherName) {
  if (result.cancelled) {
    return tr("Comparison cancelled.");
  }
  const int total = result.unchanged + result.moved + result.changed + result.added;
  if (result.moved + result.changed + result.added + result.removed == 0) {
    return tr("No differences from %1 (%2).").arg(otherName, counted(total, total == 1 ? "part" : "parts"));
  }
  QStringList parts;
  if (result.changed > 0) {
    parts << counted(result.changed, "changed");
  }
  if (result.moved > 0) {
    parts << counted(result.moved, "moved");
  }
  if (result.added > 0) {
    parts << counted(result.added, "added");
  }
  if (result.removed > 0) {
    parts << counted(result.removed, "removed");
  }
  parts << counted(result.unchanged, "unchanged");
  return tr("Compared with %1: %2.").arg(otherName, parts.join(QStringLiteral(", ")));
}

void ComparisonPanel::setRunning(int percent, const QString& stage) {
  if (!_running) {
    _tree->clear();
    _result.reset();
  }
  _running = true;
  _summary->setText(stage.isEmpty() ? tr("Comparing…") : stage + QStringLiteral("…"));
  _progress->setValue(percent);
  _progress->setVisible(true);
  _cancel->setVisible(true);
  _again->setEnabled(false);
  _end->setEnabled(false);
}

void ComparisonPanel::setResult(std::shared_ptr<const core::ComparisonResult> result,
                                const QString& otherName, const core::MeasureDisplayOptions& units) {
  _running = false;
  _progress->setVisible(false);
  _cancel->setVisible(false);
  _result = std::move(result);
  _units = units;
  _summary->setText(_result ? summaryOf(*_result, otherName) : idleText());
  const bool shown = _result && !_result->cancelled;
  _again->setEnabled(shown);
  _end->setEnabled(shown);
  fillRows();
}

void ComparisonPanel::setError(const QString& message) {
  clear();
  _summary->setText(message);
}

void ComparisonPanel::clear() {
  _running = false;
  _progress->setVisible(false);
  _cancel->setVisible(false);
  _again->setEnabled(false);
  _end->setEnabled(false);
  _result.reset();
  _tree->clear();
  _summary->setText(idleText());
}

void ComparisonPanel::fillRows() {
  _tree->clear();
  if (!_result || _result->cancelled) {
    return;
  }
  QList<QTreeWidgetItem*> rows;
  for (size_t i = 0; i < _result->changes.size(); ++i) {
    const core::PartChange& change = _result->changes[i];
    if (change.kind == core::ChangeKind::Unchanged && !_showUnchanged->isChecked()) {
      continue;
    }
    const QString deviation = change.kind == core::ChangeKind::Changed
                                  ? QString::fromStdString(core::formatLength(
                                        change.maxDeviation, _units.lengthUnit, _units.decimals))
                                  : QString();
    auto* item = new QTreeWidgetItem(
        {QString::fromStdString(change.path), QString::fromLatin1(core::toString(change.kind)), deviation});
    item->setData(0, ChangeRole, static_cast<int>(i));
    item->setData(1, Qt::DecorationRole, swatchOf(change.kind));
    item->setToolTip(0, item->text(0));
    rows.append(item);
  }
  _tree->addTopLevelItems(rows);
  _tree->resizeColumnToContents(1);
}

QString ComparisonPanel::summary() const {
  return _summary->text();
}

int ComparisonPanel::rowCount() const {
  return _tree->topLevelItemCount();
}

int ComparisonPanel::currentChange() const {
  const QTreeWidgetItem* item = _tree->currentItem();
  return item == nullptr ? -1 : item->data(0, ChangeRole).toInt();
}

void ComparisonPanel::setCurrentRow(int row) {
  _tree->setCurrentItem(row >= 0 && row < rowCount() ? _tree->topLevelItem(row) : nullptr);
}

double ComparisonPanel::tolerance() const {
  return _tolerance->value();
}

void ComparisonPanel::setShowUnchanged(bool show) {
  _showUnchanged->setChecked(show);
}

} // namespace sc::app
