#include "app/MarkupPanel.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace sc::app {

namespace {
constexpr int IdRole = Qt::UserRole;
} // namespace

MarkupPanel::MarkupPanel(QWidget* parent) : QWidget(parent) {
  _hint = new QLabel(tr("Add a note or a sketch from the Markup menu."), this);
  _hint->setObjectName(QStringLiteral("markupHint"));
  _hint->setWordWrap(true);

  _list = new QListWidget(this);
  _list->setObjectName(QStringLiteral("markupList"));
  _list->setSelectionMode(QAbstractItemView::ExtendedSelection);
  _list->setAlternatingRowColors(true);

  _delete = new QToolButton(this);
  _delete->setObjectName(QStringLiteral("markupDelete"));
  _delete->setText(tr("Delete"));
  _delete->setToolTip(tr("Delete the chosen notes and sketches (Delete)"));

  auto* removeKey = new QAction(this);
  removeKey->setShortcut(QKeySequence::Delete);
  removeKey->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  _list->addAction(removeKey);

  auto* buttons = new QHBoxLayout();
  buttons->addStretch(1);
  buttons->addWidget(_delete);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->addWidget(_hint);
  layout->addWidget(_list, 1);
  layout->addLayout(buttons);

  const auto requestDelete = [this] {
    if (!selectedIds().empty()) {
      Q_EMIT deleteRequested();
    }
  };
  connect(_delete, &QToolButton::clicked, this, requestDelete);
  connect(removeKey, &QAction::triggered, this, requestDelete);
  connect(_list, &QListWidget::currentItemChanged, this, [this] { Q_EMIT itemChosen(currentId()); });
  connect(_list, &QListWidget::itemSelectionChanged, this, &MarkupPanel::updateButtons);
  connect(_list, &QListWidget::itemActivated, this,
          [this](QListWidgetItem* item) { Q_EMIT itemActivated(item->data(IdRole).toInt()); });
  updateButtons();
}

QString MarkupPanel::noteLabel(const std::string& text) {
  const QString all = QString::fromStdString(text).trimmed();
  const qsizetype end = all.indexOf(QLatin1Char('\n'));
  return end < 0 ? all : all.left(end).trimmed() + QStringLiteral(" ...");
}

void MarkupPanel::setMarkup(const core::Markup& markup, const std::function<bool(int)>& isMissing) {
  const int current = currentId();
  {
    const QSignalBlocker blocker(_list);
    _list->clear();
    const QString missing = tr(" (part missing)");
    for (const core::MarkupNote& note : markup.notes()) {
      auto* item =
          new QListWidgetItem(noteLabel(note.text) + (isMissing && isMissing(note.id) ? missing : QString()));
      item->setData(IdRole, note.id);
      item->setToolTip(QString::fromStdString(note.text));
      _list->addItem(item);
    }
    int number = 0;
    for (const core::MarkupStroke& stroke : markup.strokes()) {
      auto* item = new QListWidgetItem(tr("Sketch %1").arg(++number) +
                                       (isMissing && isMissing(stroke.id) ? missing : QString()));
      item->setData(IdRole, stroke.id);
      _list->addItem(item);
    }
  }
  _hint->setVisible(markup.empty());
  setCurrentId(current);
  updateButtons();
}

void MarkupPanel::clear() {
  setMarkup(core::Markup(), {});
}

int MarkupPanel::rowCount() const {
  return _list->count();
}

QString MarkupPanel::rowText(int row) const {
  const QListWidgetItem* item = _list->item(row);
  return item != nullptr ? item->text() : QString();
}

int MarkupPanel::idAt(int row) const {
  const QListWidgetItem* item = _list->item(row);
  return item != nullptr ? item->data(IdRole).toInt() : 0;
}

int MarkupPanel::currentId() const {
  const QListWidgetItem* item = _list->currentItem();
  return item != nullptr && item->isSelected() ? item->data(IdRole).toInt() : 0;
}

void MarkupPanel::setCurrentId(int id) {
  for (int row = 0; row < _list->count(); ++row) {
    if (idAt(row) == id && id != 0) {
      _list->setCurrentRow(row, QItemSelectionModel::ClearAndSelect);
      return;
    }
  }
  if (currentId() != 0) {
    _list->setCurrentRow(-1, QItemSelectionModel::Clear);
  }
}

std::vector<int> MarkupPanel::selectedIds() const {
  std::vector<int> ids;
  for (int row = 0; row < _list->count(); ++row) {
    if (_list->item(row)->isSelected()) {
      ids.push_back(idAt(row));
    }
  }
  return ids;
}

void MarkupPanel::updateButtons() {
  _delete->setEnabled(!_list->selectedItems().isEmpty());
}

} // namespace sc::app
