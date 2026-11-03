// PmiTreeModel and PmiPanel: groups, visibility, filtering and signals.
//
// Runs on the offscreen platform (no display, no OpenGL). The shared PMI fixture
// has five items: 1 dimension ("Height"), 2 tolerances (flatness, parallelism),
// 1 datum (A) and 1 not-interpreted annotation, which was read as a dimension
// but is listed only under "Not Interpreted", plus one saved view ("Front").

#include "PmiFixture.h"
#include "app/PmiPanel.h"
#include "app/PmiTreeModel.h"

#include <QApplication>
#include <QComboBox>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTest>
#include <QTreeView>

#include <algorithm>
#include <memory>

using sc::app::PmiPanel;
using sc::app::PmiTreeModel;
using sc::core::PmiId;
using sc::core::PmiKind;
using IdList = std::vector<PmiId>;

namespace {

std::shared_ptr<const sc::core::PmiModel> fixtureModel() {
  const sc::test::PmiFixture f = sc::test::buildPmiFixture();
  return std::make_shared<const sc::core::PmiModel>(f.document);
}

IdList sorted(IdList ids) {
  std::sort(ids.begin(), ids.end());
  return ids;
}

// Titles of the top-level rows of a model.
QStringList groupTitles(const QAbstractItemModel& model) {
  QStringList titles;
  for (int row = 0; row < model.rowCount(); ++row) {
    titles << model.index(row, 0).data().toString();
  }
  return titles;
}

QModelIndex groupIndex(const QAbstractItemModel& model, const QString& prefix) {
  for (int row = 0; row < model.rowCount(); ++row) {
    const QModelIndex index = model.index(row, 0);
    if (index.data().toString().startsWith(prefix)) {
      return index;
    }
  }
  return {};
}

IdList childIds(const PmiTreeModel& model, const QModelIndex& group) {
  IdList ids;
  for (int row = 0; row < model.rowCount(group); ++row) {
    ids.push_back(model.pmiId(model.index(row, 0, group)));
  }
  return ids;
}

// Id of the item of the given kind and type name.
PmiId findItem(const sc::core::PmiModel& pmi, PmiKind kind, const std::string& typeName) {
  for (const auto& item : pmi.items()) {
    if (item.kind == kind && item.typeName == typeName) {
      return item.id;
    }
  }
  return sc::core::kNoPmi;
}

} // namespace

class PmiPanelTest : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void groupsAndCounts();
  void rowContent();
  void viewRowHasNoCheckBox();
  void groupCheckBoxTogglesChildren();
  void childCheckBoxUpdatesGroupState();
  void programmaticVisibilityDoesNotEmit();
  void panelForwardsVisibilityRequests();
  void textFilter();
  void kindFilter();
  void selectionSignals();
  void viewActivation();
  void emptyModel();
};

void PmiPanelTest::groupsAndCounts() {
  const auto pmi = fixtureModel();
  PmiTreeModel model;
  model.setModel(pmi);

  // Groups in fixed order; counts in the title; the not-interpreted item is
  // listed only under "Not Interpreted", so Dimensions has 1 (not 2) entries.
  QCOMPARE(groupTitles(model), (QStringList{"Saved Views (1)", "Dimensions (1)", "Geometric Tolerances (2)",
                                            "Datums (1)", "Not Interpreted (1)"}));
  QCOMPARE(model.columnCount(), 2);

  // Every item appears exactly once.
  IdList all;
  for (const char* prefix : {"Dimensions", "Geometric", "Datums", "Not Interpreted"}) {
    const IdList ids = childIds(model, groupIndex(model, prefix));
    all.insert(all.end(), ids.begin(), ids.end());
  }
  QCOMPARE(all.size(), pmi->items().size());
  QCOMPARE(sorted(all), model.visibleIds()); // all start visible

  const IdList notInterpreted = childIds(model, groupIndex(model, "Not Interpreted"));
  QCOMPARE(notInterpreted.size(), size_t{1});
  QVERIFY(!pmi->item(notInterpreted.front()).semantic);

  // Empty groups are omitted.
  PmiTreeModel empty;
  empty.setModel(std::make_shared<const sc::core::PmiModel>(nullptr));
  QCOMPARE(empty.rowCount(), 0);
}

void PmiPanelTest::rowContent() {
  const auto pmi = fixtureModel();
  PmiTreeModel model;
  model.setModel(pmi);

  const PmiId height = findItem(*pmi, PmiKind::Dimension, "Linear distance");
  QVERIFY2(height != sc::core::kNoPmi, "fixture has a linear distance");
  QModelIndex row = model.indexOf(height);
  QVERIFY(row.isValid());
  QCOMPARE(model.pmiId(row), height);
  QCOMPARE(model.viewId(row), -1);
  QCOMPARE(row.data(PmiTreeModel::PmiIdRole).toInt(), height);
  QCOMPARE(row.data().toString(), QStringLiteral("Height"));
  QCOMPARE(row.siblingAtColumn(1).data().toString(), QString::fromStdString(pmi->item(height).text));
  QCOMPARE(row.data(Qt::ToolTipRole).toString(),
           QString::fromStdString(pmi->item(height).typeName + ": " + pmi->item(height).text));
  QCOMPARE(row.parent(), groupIndex(model, "Dimensions"));

  // Group rows carry no PmiId.
  QCOMPARE(model.pmiId(groupIndex(model, "Datums")), sc::core::kNoPmi);
  QVERIFY(!groupIndex(model, "Datums").data(PmiTreeModel::PmiIdRole).isValid());
  QCOMPARE(model.indexOf(9999), QModelIndex());
}

void PmiPanelTest::viewRowHasNoCheckBox() {
  const auto pmi = fixtureModel();
  PmiTreeModel model;
  model.setModel(pmi);

  const QModelIndex views = groupIndex(model, "Saved Views");
  QVERIFY(!views.data(Qt::CheckStateRole).isValid());
  QVERIFY(!(model.flags(views) & Qt::ItemIsUserCheckable));

  const QModelIndex view = model.indexOfView(pmi->views().front().id);
  QVERIFY(view.isValid());
  QCOMPARE(view.parent(), views);
  QCOMPARE(view.data().toString(), QStringLiteral("Front"));
  QCOMPARE(model.viewId(view), pmi->views().front().id);
  QCOMPARE(view.data(PmiTreeModel::ViewIdRole).toInt(), pmi->views().front().id);
  QVERIFY(!view.data(Qt::CheckStateRole).isValid());
  QVERIFY(!(model.flags(view) & Qt::ItemIsUserCheckable));
  QVERIFY(model.flags(view) & Qt::ItemIsSelectable);
  QCOMPARE(model.pmiId(view), sc::core::kNoPmi);
  QVERIFY(!model.setData(view, Qt::Unchecked, Qt::CheckStateRole));

  // PMI rows and their groups do have one.
  const QModelIndex dims = groupIndex(model, "Dimensions");
  QVERIFY(dims.data(Qt::CheckStateRole).isValid());
  QVERIFY(model.index(0, 0, dims).data(Qt::CheckStateRole).isValid());
  QVERIFY(model.flags(dims) & Qt::ItemIsUserCheckable);
}

void PmiPanelTest::groupCheckBoxTogglesChildren() {
  const auto pmi = fixtureModel();
  PmiTreeModel model;
  model.setModel(pmi);
  qRegisterMetaType<IdList>("std::vector<sc::core::PmiId>");
  QSignalSpy spy(&model, &PmiTreeModel::visibilityRequested);
  QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

  const QModelIndex tolerances = groupIndex(model, "Geometric");
  const IdList ids = childIds(model, tolerances);
  QCOMPARE(ids.size(), size_t{2});
  QCOMPARE(tolerances.data(Qt::CheckStateRole).toInt(), int(Qt::Checked));

  QVERIFY(model.setData(tolerances, Qt::Unchecked, Qt::CheckStateRole));
  QCOMPARE(spy.count(), 1);
  QCOMPARE(sorted(spy.at(0).at(0).value<IdList>()), sorted(ids));
  QCOMPARE(spy.at(0).at(1).toBool(), false);
  QVERIFY(changed.count() > 0);

  // The model updated itself before signalling.
  QCOMPARE(tolerances.data(Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
  for (const PmiId id : ids) {
    QVERIFY(!model.isVisible(id));
    QCOMPARE(model.indexOf(id).data(Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
  }
  QCOMPARE(model.visibleIds().size(), pmi->items().size() - ids.size());

  // A partly checked group turns fully checked when clicked.
  model.setVisible({ids.front()}, true);
  QCOMPARE(tolerances.data(Qt::CheckStateRole).toInt(), int(Qt::PartiallyChecked));
  QVERIFY(model.setData(tolerances, Qt::Checked, Qt::CheckStateRole));
  QCOMPARE(spy.count(), 2);
  QCOMPARE(spy.at(1).at(1).toBool(), true);
  QCOMPARE(sorted(spy.at(1).at(0).value<IdList>()), sorted(ids));
  QCOMPARE(tolerances.data(Qt::CheckStateRole).toInt(), int(Qt::Checked));
}

void PmiPanelTest::childCheckBoxUpdatesGroupState() {
  const auto pmi = fixtureModel();
  PmiTreeModel model;
  model.setModel(pmi);
  qRegisterMetaType<IdList>("std::vector<sc::core::PmiId>");
  QSignalSpy spy(&model, &PmiTreeModel::visibilityRequested);

  const QModelIndex tolerances = groupIndex(model, "Geometric");
  const IdList ids = childIds(model, tolerances);
  const QModelIndex first = model.indexOf(ids[0]);
  const QModelIndex second = model.indexOf(ids[1]);

  QVERIFY(model.setData(first, Qt::Unchecked, Qt::CheckStateRole));
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).value<IdList>(), IdList{ids[0]});
  QCOMPARE(spy.at(0).at(1).toBool(), false);
  QCOMPARE(first.data(Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
  QCOMPARE(second.data(Qt::CheckStateRole).toInt(), int(Qt::Checked));
  QCOMPARE(tolerances.data(Qt::CheckStateRole).toInt(), int(Qt::PartiallyChecked));

  QVERIFY(model.setData(second, Qt::Unchecked, Qt::CheckStateRole));
  QCOMPARE(tolerances.data(Qt::CheckStateRole).toInt(), int(Qt::Unchecked));

  QVERIFY(model.setData(first, Qt::Checked, Qt::CheckStateRole));
  QVERIFY(model.setData(second, Qt::Checked, Qt::CheckStateRole));
  QCOMPARE(tolerances.data(Qt::CheckStateRole).toInt(), int(Qt::Checked));
  QCOMPARE(spy.count(), 4);

  // Other groups are untouched, and other roles are not editable.
  QCOMPARE(groupIndex(model, "Datums").data(Qt::CheckStateRole).toInt(), int(Qt::Checked));
  QVERIFY(!model.setData(first, QStringLiteral("x"), Qt::EditRole));
  QCOMPARE(spy.count(), 4);
}

void PmiPanelTest::programmaticVisibilityDoesNotEmit() {
  const auto pmi = fixtureModel();
  PmiTreeModel model;
  model.setModel(pmi);
  qRegisterMetaType<IdList>("std::vector<sc::core::PmiId>");
  QSignalSpy requested(&model, &PmiTreeModel::visibilityRequested);
  QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

  IdList everything;
  for (const auto& item : pmi->items()) {
    everything.push_back(item.id);
  }
  model.setVisible(everything, false);
  QCOMPARE(requested.count(), 0);
  QVERIFY(changed.count() > 0);
  QVERIFY(model.visibleIds().empty());
  QCOMPARE(groupIndex(model, "Dimensions").data(Qt::CheckStateRole).toInt(), int(Qt::Unchecked));

  changed.clear();
  model.setVisible({everything.front(), 9999, -3}, true); // unknown ids are ignored
  QCOMPARE(requested.count(), 0);
  QVERIFY(changed.count() > 0);
  QCOMPARE(model.visibleIds(), IdList{everything.front()});

  changed.clear();
  model.setVisible({everything.front()}, true); // no change, no signal
  QCOMPARE(changed.count(), 0);

  // A model can also start hidden.
  model.setModel(pmi, false);
  QVERIFY(model.visibleIds().empty());
  QCOMPARE(groupIndex(model, "Datums").data(Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
}

void PmiPanelTest::panelForwardsVisibilityRequests() {
  PmiPanel panel;
  panel.setModel(fixtureModel());
  qRegisterMetaType<IdList>("std::vector<sc::core::PmiId>");
  QSignalSpy requested(&panel, &PmiPanel::visibilityRequested);

  auto* tree = panel.findChild<QTreeView*>();
  QVERIFY(tree);
  const QModelIndex datums = tree->model()->index(3, 0); // Saved Views, Dimensions, Tolerances, Datums
  QVERIFY(datums.data().toString().startsWith("Datums"));
  QVERIFY(tree->model()->setData(datums, Qt::Unchecked, Qt::CheckStateRole)); // through the proxy
  QCOMPARE(requested.count(), 1);
  QCOMPARE(requested.at(0).at(0).value<IdList>().size(), size_t{1});
  QCOMPARE(requested.at(0).at(1).toBool(), false);
  QCOMPARE(panel.visibleIds().size(), size_t{4});

  // Programmatic changes through the panel update the state without a request.
  panel.setVisible(requested.at(0).at(0).value<IdList>(), true);
  QCOMPARE(requested.count(), 1);
  QCOMPARE(panel.visibleIds().size(), size_t{5});
  QCOMPARE(datums.data(Qt::CheckStateRole).toInt(), int(Qt::Checked));
}

void PmiPanelTest::textFilter() {
  PmiPanel panel;
  panel.show();
  panel.setModel(fixtureModel());
  auto* tree = panel.findChild<QTreeView*>();
  auto* search = panel.findChild<QLineEdit*>();
  QVERIFY(tree && search);
  QCOMPARE(search->placeholderText(), QString::fromUtf8("Filter PMI\xE2\x80\xA6"));
  QAbstractItemModel* shown = tree->model();
  QCOMPARE(shown->rowCount(), 5);

  // "flat" (case-insensitive) leaves the flatness row and its group.
  search->setText("FLAT");
  QCOMPARE(shown->rowCount(), 1);
  const QModelIndex group = shown->index(0, 0);
  QVERIFY(group.data().toString().startsWith("Geometric Tolerances"));
  QCOMPARE(shown->rowCount(group), 1);
  QVERIFY(group.model()->index(0, 0, group).data().toString().contains("Flatness", Qt::CaseInsensitive));
  QVERIFY(tree->isExpanded(group));

  // The text column matches too ("A" datum text, here via the datum's name).
  search->setText("datum a");
  QCOMPARE(shown->rowCount(), 1);
  QVERIFY(shown->index(0, 0).data().toString().startsWith("Datums"));

  search->setText("no such annotation");
  QCOMPARE(shown->rowCount(), 0);

  search->clear();
  QCOMPARE(shown->rowCount(), 5);
}

void PmiPanelTest::kindFilter() {
  PmiPanel panel;
  panel.setModel(fixtureModel());
  auto* tree = panel.findChild<QTreeView*>();
  auto* kind = panel.findChild<QComboBox*>();
  auto* search = panel.findChild<QLineEdit*>();
  QVERIFY(tree && kind && search);
  QAbstractItemModel* shown = tree->model();

  QStringList entries;
  for (int i = 0; i < kind->count(); ++i) {
    entries << kind->itemText(i);
  }
  QCOMPARE(entries, (QStringList{"All", "Dimensions", "Geometric tolerances", "Datums", "Not interpreted"}));

  const auto showOnly = [&](const QString& text) {
    kind->setCurrentIndex(kind->findText(text));
    return groupTitles(*shown);
  };
  QCOMPARE(showOnly("Dimensions"), QStringList{"Dimensions (1)"});
  QCOMPARE(showOnly("Geometric tolerances"), QStringList{"Geometric Tolerances (2)"});
  QCOMPARE(showOnly("Datums"), QStringList{"Datums (1)"});
  QCOMPARE(showOnly("Not interpreted"), QStringList{"Not Interpreted (1)"});
  QCOMPARE(shown->rowCount(shown->index(0, 0)), 1);
  QCOMPARE(showOnly("All").size(), 5);

  // Kind and text filters combine.
  kind->setCurrentIndex(kind->findText("Datums"));
  search->setText("flat");
  QCOMPARE(shown->rowCount(), 0);
  kind->setCurrentIndex(kind->findText("Geometric tolerances"));
  QCOMPARE(shown->rowCount(), 1);
}

void PmiPanelTest::selectionSignals() {
  const auto pmi = fixtureModel();
  PmiPanel panel;
  panel.show();
  panel.setModel(pmi);
  qRegisterMetaType<IdList>("std::vector<sc::core::PmiId>");
  QSignalSpy selected(&panel, &PmiPanel::pmiSelected);

  auto* tree = panel.findChild<QTreeView*>();
  QVERIFY(tree);
  const QAbstractItemModel* shown = tree->model();
  const QModelIndex tolerances = shown->index(2, 0);
  QVERIFY(tolerances.data().toString().startsWith("Geometric"));
  const QModelIndex flatness = shown->index(0, 0, tolerances);
  const QModelIndex parallelism = shown->index(1, 0, tolerances);
  const PmiId flatnessId = flatness.data(PmiTreeModel::PmiIdRole).toInt();
  const PmiId parallelismId = parallelism.data(PmiTreeModel::PmiIdRole).toInt();

  // User selection: emitted, with only PMI rows (the group is dropped).
  QItemSelectionModel* selection = tree->selectionModel();
  selection->select(flatness, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  QCOMPARE(selected.count(), 1);
  QCOMPARE(selected.at(0).at(0).value<IdList>(), IdList{flatnessId});

  selection->select(tolerances, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  QCOMPARE(selected.count(), 2);
  QVERIFY(selected.at(1).at(0).value<IdList>().empty());

  selected.clear();
  selection->clearSelection();

  // Programmatic selection: selects and scrolls, but does not emit.
  selected.clear();
  panel.selectPmi({parallelismId, flatnessId});
  QCOMPARE(selected.count(), 0);
  const QModelIndexList rows = selection->selectedRows(0);
  QCOMPARE(rows.size(), 2);
  QVERIFY(selection->isSelected(flatness));
  QVERIFY(selection->isSelected(parallelism));
  QCOMPARE(selection->currentIndex(), parallelism);

  panel.selectPmi({});
  QCOMPARE(selected.count(), 0);
  QVERIFY(selection->selectedRows(0).isEmpty());

  // The guard does not stick: user selection still emits afterwards.
  selection->select(parallelism, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  QCOMPARE(selected.count(), 1);
  QCOMPARE(selected.at(0).at(0).value<IdList>(), IdList{parallelismId});

  // Selecting a row hidden by the filter is skipped.
  selected.clear();
  panel.findChild<QLineEdit*>()->setText("zzz");
  selected.clear();
  panel.selectPmi({flatnessId});
  QCOMPARE(selected.count(), 0);
  QVERIFY(selection->selectedRows(0).isEmpty());
}

void PmiPanelTest::viewActivation() {
  const auto pmi = fixtureModel();
  PmiPanel panel;
  panel.setModel(pmi);
  QSignalSpy activated(&panel, &PmiPanel::viewActivated);

  auto* tree = panel.findChild<QTreeView*>();
  QVERIFY(tree);
  const QAbstractItemModel* shown = tree->model();
  const QModelIndex views = shown->index(0, 0);
  QVERIFY(views.data().toString().startsWith("Saved Views"));
  const QModelIndex view = shown->index(0, 0, views);
  QCOMPARE(view.data().toString(), QStringLiteral("Front"));

  // Activating groups and PMI rows does nothing.
  Q_EMIT tree->activated(views);
  Q_EMIT tree->activated(shown->index(0, 0, shown->index(1, 0)));
  QCOMPARE(activated.count(), 0);

  Q_EMIT tree->activated(view);
  QCOMPARE(activated.count(), 1);
  QCOMPARE(activated.at(0).at(0).toInt(), pmi->views().front().id);
}

void PmiPanelTest::emptyModel() {
  PmiPanel panel;
  auto* summary = panel.findChild<QLabel*>();
  auto* tree = panel.findChild<QTreeView*>();
  QVERIFY(summary && tree);

  panel.setModel(std::make_shared<const sc::core::PmiModel>(nullptr));
  QCOMPARE(summary->text(), QStringLiteral("No PMI in this file"));
  QCOMPARE(tree->model()->rowCount(), 0);
  QVERIFY(panel.visibleIds().empty());

  panel.setModel(fixtureModel());
  QCOMPARE(summary->text(), QString::fromUtf8("5 annotations \xC2\xB7 1 saved view"));
  QCOMPARE(tree->model()->rowCount(), 5);
  QVERIFY(tree->isExpanded(tree->model()->index(1, 0)));

  panel.clear();
  QCOMPARE(tree->model()->rowCount(), 0);
  QVERIFY(summary->text().isEmpty());
}

int main(int argc, char** argv) {
  // No display or OpenGL is needed; always run offscreen.
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  PmiPanelTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_pmi_panel.moc"
