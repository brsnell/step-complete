#include "app/GeometryCheckPanel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <map>

namespace sc::app {

namespace {

constexpr int IndexRole = Qt::UserRole + 1;

QString qs(const std::string& text) {
  return QString::fromStdString(text);
}

QString plural(int count, const char* one, const char* many) {
  return QStringLiteral("%1 %2").arg(count).arg(QLatin1String(count == 1 ? one : many));
}

} // namespace

GeometryCheckPanel::GeometryCheckPanel(QWidget* parent) : QWidget(parent) {
  _summary =
      new QLabel(tr("Measure > Check Geometry checks the selected parts, or every visible part."), this);
  _summary->setObjectName(QStringLiteral("geometryCheckSummary"));
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
  _rerun->setToolTip(tr("Check the selected parts, or every visible part, again"));
  _rerun->setAutoRaise(true);
  _rerun->setEnabled(false);

  _tree = new QTreeWidget(this);
  _tree->setObjectName(QStringLiteral("geometryCheckList"));
  _tree->setColumnCount(3);
  _tree->setHeaderLabels({tr("Part"), tr("Problem"), tr("Detail")});
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

  connect(_cancel, &QToolButton::clicked, this, &GeometryCheckPanel::cancelRequested);
  connect(_rerun, &QToolButton::clicked, this, &GeometryCheckPanel::rerunRequested);
  connect(_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
    Q_EMIT issueChosen(item == nullptr ? -1 : item->data(0, IndexRole).toInt());
  });
  connect(_tree, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem* item) { Q_EMIT issueActivated(item->data(0, IndexRole).toInt()); });
}

QString GeometryCheckPanel::summaryOf(const core::GeometryCheckResult& result) {
  if (result.cancelled) {
    return tr("Geometry check cancelled.");
  }
  if (result.issueCount == 0) {
    return tr("No problems found in %1.").arg(plural(result.partsChecked, "part", "parts"));
  }
  return tr("%1 in %2 of %3.")
      .arg(plural(result.issueCount, "problem", "problems"))
      .arg(result.partsWithIssues)
      .arg(plural(result.partsChecked, "part", "parts"));
}

void GeometryCheckPanel::setRunning(int percent, const QString& stage) {
  if (!_running) {
    _tree->clear();
    _issues.clear();
  }
  _running = true;
  _summary->setText(stage.isEmpty() ? tr("Checking geometry…") : stage + QStringLiteral("…"));
  _progress->setValue(percent);
  _progress->setVisible(true);
  _cancel->setVisible(true);
  _rerun->setEnabled(false);
}

void GeometryCheckPanel::setResult(const core::GeometryCheckResult& result,
                                   const core::AssemblyIndex& index) {
  _running = false;
  _progress->setVisible(false);
  _cancel->setVisible(false);
  _rerun->setEnabled(true);
  _tree->clear();
  _issues = result.issues;

  QString text = summaryOf(result);
  if (static_cast<int>(_issues.size()) < result.issueCount) {
    text +=
        QLatin1Char(' ') + tr("Only the first %1 problems of a part are listed.").arg(core::MaxIssuesPerPart);
  }
  _summary->setText(text);

  QList<QTreeWidgetItem*> rows;
  rows.reserve(static_cast<qsizetype>(_issues.size()));
  for (size_t i = 0; i < _issues.size(); ++i) {
    const core::GeometryIssue& issue = _issues[i];
    QString part = issue.part == core::NoNode ? QString() : qs(index.node(issue.part).name);
    if (issue.instances > 1) {
      part += QStringLiteral(" (×%1)").arg(issue.instances);
    }
    auto* item = new QTreeWidgetItem({part, QString::fromLatin1(core::toString(issue.kind)),
                                      qs(issue.shapeType) + QStringLiteral(": ") + qs(issue.detail)});
    item->setData(0, IndexRole, static_cast<int>(i));
    item->setToolTip(0, part);
    item->setToolTip(2, item->text(2));
    rows.append(item);
  }
  _tree->addTopLevelItems(rows);
  for (int column = 0; column < 2; ++column) {
    _tree->resizeColumnToContents(column);
  }
}

void GeometryCheckPanel::clear() {
  _running = false;
  _progress->setVisible(false);
  _cancel->setVisible(false);
  _rerun->setEnabled(false);
  _tree->clear();
  _issues.clear();
  _summary->setText(tr("Measure > Check Geometry checks the selected parts, or every visible part."));
}

QString GeometryCheckPanel::summary() const {
  return _summary->text();
}

int GeometryCheckPanel::rowCount() const {
  return _tree->topLevelItemCount();
}

int GeometryCheckPanel::currentIndex() const {
  const QTreeWidgetItem* item = _tree->currentItem();
  return item == nullptr ? -1 : item->data(0, IndexRole).toInt();
}

void GeometryCheckPanel::setCurrentIndex(int index) {
  _tree->setCurrentItem(index >= 0 && index < rowCount() ? _tree->topLevelItem(index) : nullptr);
}

} // namespace sc::app
