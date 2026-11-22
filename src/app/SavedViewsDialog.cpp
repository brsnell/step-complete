#include "app/SavedViewsDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace sc::app {

SavedViewsDialog::SavedViewsDialog(std::vector<UserView> views, QWidget* parent)
    : QDialog(parent), _views(std::move(views)) {
  setWindowTitle(tr("Saved Views"));

  _list = new QListWidget(this);
  _list->setObjectName(QStringLiteral("savedViewsList"));
  _rename = new QPushButton(tr("&Rename"), this);
  _delete = new QPushButton(tr("&Delete"), this);
  _up = new QPushButton(tr("Move &Up"), this);
  _down = new QPushButton(tr("Move D&own"), this);

  auto* side = new QVBoxLayout;
  for (QPushButton* button : {_rename, _delete, _up, _down}) {
    side->addWidget(button);
  }
  side->addStretch(1);
  auto* body = new QHBoxLayout;
  body->addWidget(_list, 1);
  body->addLayout(side);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(body);
  layout->addWidget(buttons);

  connect(_rename, &QPushButton::clicked, this, [this] {
    if (QListWidgetItem* item = _list->currentItem()) {
      _list->editItem(item);
    }
  });
  connect(_delete, &QPushButton::clicked, this, &SavedViewsDialog::deleteCurrent);
  connect(_up, &QPushButton::clicked, this, [this] { moveCurrent(-1); });
  connect(_down, &QPushButton::clicked, this, [this] { moveCurrent(1); });
  connect(_list, &QListWidget::itemChanged, this, &SavedViewsDialog::onItemChanged);
  connect(_list, &QListWidget::currentRowChanged, this, &SavedViewsDialog::updateButtons);

  rebuild(0);
}

void SavedViewsDialog::rebuild(int currentRow) {
  _rebuilding = true;
  _list->clear();
  for (const UserView& view : _views) {
    auto* item = new QListWidgetItem(view.name, _list);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
  }
  _rebuilding = false;
  _list->setCurrentRow(_views.empty() ? -1 : std::clamp(currentRow, 0, static_cast<int>(_views.size()) - 1));
  updateButtons();
}

void SavedViewsDialog::onItemChanged(QListWidgetItem* item) {
  if (_rebuilding) {
    return;
  }
  const int row = _list->row(item);
  if (row < 0 || row >= static_cast<int>(_views.size())) {
    return;
  }
  const QString name = item->text().trimmed();
  const bool taken = std::any_of(_views.begin(), _views.end(), [&](const UserView& other) {
    return &other != &_views[static_cast<size_t>(row)] && other.name.compare(name, Qt::CaseInsensitive) == 0;
  });
  if (!name.isEmpty() && !taken) {
    _views[static_cast<size_t>(row)].name = name;
  }
  rebuild(row); // shows the trimmed name, or puts back the old one
}

void SavedViewsDialog::renameCurrent(const QString& name) {
  if (QListWidgetItem* item = _list->currentItem()) {
    item->setText(name); // goes through onItemChanged, like an edit in place
  }
}

void SavedViewsDialog::deleteCurrent() {
  const int row = _list->currentRow();
  if (row < 0 || row >= static_cast<int>(_views.size())) {
    return;
  }
  _views.erase(_views.begin() + row);
  rebuild(row);
}

void SavedViewsDialog::moveCurrent(int step) {
  const int row = _list->currentRow();
  const int target = row + step;
  if (row < 0 || target < 0 || target >= static_cast<int>(_views.size())) {
    return;
  }
  std::swap(_views[static_cast<size_t>(row)], _views[static_cast<size_t>(target)]);
  rebuild(target);
}

void SavedViewsDialog::setCurrentRow(int row) {
  _list->setCurrentRow(row);
}

void SavedViewsDialog::updateButtons() {
  const int row = _list->currentRow();
  const bool any = row >= 0;
  _rename->setEnabled(any);
  _delete->setEnabled(any);
  _up->setEnabled(any && row > 0);
  _down->setEnabled(any && row + 1 < static_cast<int>(_views.size()));
}

} // namespace sc::app
