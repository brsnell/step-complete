// AssemblyFilterProxy: which rows of the fixture assembly a filter text keeps.

#include "TestModels.h"

#include "app/AssemblyFilterProxy.h"
#include "app/AssemblyTreeModel.h"
#include "core/AssemblyIndex.h"

#include <QStringList>
#include <QtTest>

#include <memory>

using sc::app::AssemblyFilterProxy;
using sc::app::AssemblyTreeModel;

namespace {

// Every row the proxy shows, depth first, as "name" indented by two spaces per level.
QStringList shownRows(const QAbstractItemModel& model, const QModelIndex& parent = {}, int depth = 0) {
  QStringList rows;
  for (int row = 0; row < model.rowCount(parent); ++row) {
    const QModelIndex index = model.index(row, 0, parent);
    rows << QString(depth * 2, QLatin1Char(' ')) + index.data().toString();
    rows << shownRows(model, index, depth + 1);
  }
  return rows;
}

} // namespace

class AssemblyFilterTest : public QObject {
  Q_OBJECT

private:
  std::shared_ptr<const sc::core::AssemblyIndex> _index;
  std::unique_ptr<AssemblyTreeModel> _tree;
  std::unique_ptr<AssemblyFilterProxy> _proxy;

  QStringList filtered(const QString& text) {
    _proxy->setFilterText(text);
    return shownRows(*_proxy);
  }

private Q_SLOTS:
  void init() {
    _index = std::make_shared<sc::core::AssemblyIndex>(sc::test::buildFixtureAssembly());
    _tree = std::make_unique<AssemblyTreeModel>();
    _tree->setModel(_index, [](sc::core::NodeId) { return true; });
    _proxy = std::make_unique<AssemblyFilterProxy>();
    _proxy->setSourceModel(_tree.get());
  }

  void cleanup() {
    _proxy.reset();
    _tree.reset();
  }

  void emptyFilterShowsEverything() {
    const QStringList all = shownRows(*_tree);
    QCOMPARE(all.size(), 8); // the fixture's 8 nodes
    QCOMPARE(filtered(QString()), all);
    QCOMPARE(filtered(QStringLiteral("   ")), all); // blank counts as no filter
    QVERIFY(!_proxy->filtering());
  }

  void keepsMatchesAndThePathToThem() {
    const QStringList rows = filtered(QStringLiteral("plate"));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).trimmed(), QStringLiteral("Fixture"));
    QCOMPARE(rows.at(1).trimmed(), QStringLiteral("Base plate"));
  }

  void ignoresCaseIncludingNonAscii() {
    // "Pin Ø6": lower-case "ø" must match the upper-case "Ø" in the name.
    const QStringList rows = filtered(QString::fromUtf8("PIN \xC3\xB8"));
    QCOMPARE(rows.count(QString::fromUtf8("    Pin \xC3\x98"
                                          "6")),
             4); // two pins in each pair
    QVERIFY(!rows.join(QLatin1Char('|')).contains(QStringLiteral("Base plate")));
  }

  void matchingAssemblyKeepsItsParts() {
    const QStringList rows = filtered(QStringLiteral("pair"));
    QCOMPARE(rows.size(), 7); // Fixture, two pairs, four pins below them
    QVERIFY(!rows.join(QLatin1Char('|')).contains(QStringLiteral("Base plate")));
  }

  void noMatchShowsNothing() { QVERIFY(filtered(QStringLiteral("no such part")).isEmpty()); }

  void matchesOnlyRowsThatMatchThemselves() {
    _proxy->setFilterText(QStringLiteral("plate"));
    const QModelIndex fixture = _tree->index(0, 0);
    QVERIFY(!_proxy->matches(fixture)); // kept as a parent, but not a match
    bool plateMatches = false;
    for (int row = 0; row < _tree->rowCount(fixture); ++row) {
      const QModelIndex child = _tree->index(row, 0, fixture);
      if (child.data().toString() == QStringLiteral("Base plate")) {
        plateMatches = _proxy->matches(child);
      }
    }
    QVERIFY(plateMatches);
    _proxy->setFilterText(QString());
    QVERIFY(!_proxy->matches(fixture));
  }
};

int runAssemblyFilterTests(int argc, char** argv) {
  AssemblyFilterTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_assembly_filter.moc"
