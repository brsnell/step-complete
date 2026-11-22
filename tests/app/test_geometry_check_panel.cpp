// GeometryCheckPanel (Measure > Check Geometry; docs/designs/geometry-check.md): the summary line,
// the running state, one row per problem, and choosing a row.

#include "TestModels.h"
#include "app/GeometryCheckPanel.h"
#include "core/AssemblyIndex.h"

#include <BRepPrimAPI_MakeBox.hxx>

#include <QSignalSpy>
#include <QTest>
#include <QTreeWidget>

using sc::app::GeometryCheckPanel;

class GeometryCheckPanelTest : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void summarisesResults() {
    sc::core::GeometryCheckResult clean;
    clean.partsChecked = 12;
    QCOMPARE(GeometryCheckPanel::summaryOf(clean), QStringLiteral("No problems found in 12 parts."));
    clean.partsChecked = 1;
    QCOMPARE(GeometryCheckPanel::summaryOf(clean), QStringLiteral("No problems found in 1 part."));

    sc::core::GeometryCheckResult broken;
    broken.partsChecked = 12;
    broken.partsWithIssues = 2;
    broken.issueCount = 7;
    QCOMPARE(GeometryCheckPanel::summaryOf(broken), QStringLiteral("7 problems in 2 of 12 parts."));
    broken.cancelled = true;
    QCOMPARE(GeometryCheckPanel::summaryOf(broken), QStringLiteral("Geometry check cancelled."));
  }

  void listsProblemsAndEmitsTheChosenOne() {
    const sc::core::AssemblyIndex index(sc::test::buildFixtureAssembly());
    const sc::core::NodeId part = index.partInstances().front();

    GeometryCheckPanel panel;
    QSignalSpy cancelled(&panel, &GeometryCheckPanel::cancelRequested);
    panel.setRunning(40, QStringLiteral("Checking geometry"));
    QVERIFY(panel.isRunning());
    QCOMPARE(panel.summary(), QStringLiteral("Checking geometry…"));

    sc::core::GeometryCheckResult result;
    result.partsChecked = 2;
    result.partsWithIssues = 1;
    for (int i = 0; i < 3; ++i) {
      sc::core::GeometryIssue issue;
      issue.part = part;
      issue.instances = 4;
      issue.kind = sc::core::IssueKind::TinyEdge;
      issue.shapeType = "Edge";
      issue.detail = "0.000500 mm long";
      issue.shape = BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape();
      result.issues.push_back(issue);
    }
    result.issueCount = 250; // more were found than listed
    panel.setResult(result, index);
    QVERIFY(!panel.isRunning());
    QCOMPARE(panel.rowCount(), 3);
    QVERIFY(panel.summary().startsWith(QStringLiteral("250 problems in 1 of 2 parts.")));
    QVERIFY(panel.summary().contains(QStringLiteral("first 200")));

    const auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("geometryCheckList"));
    QVERIFY(tree != nullptr);
    const QTreeWidgetItem* row = tree->topLevelItem(0);
    QCOMPARE(row->text(0), QString::fromStdString(index.node(part).name) + QStringLiteral(" (×4)"));
    QCOMPARE(row->text(1), QStringLiteral("Tiny edge"));
    QCOMPARE(row->text(2), QStringLiteral("Edge: 0.000500 mm long"));

    QSignalSpy chosen(&panel, &GeometryCheckPanel::issueChosen);
    panel.setCurrentIndex(1);
    QCOMPARE(chosen.count(), 1);
    QCOMPARE(chosen.takeFirst().at(0).toInt(), 1);
    QCOMPARE(panel.currentIndex(), 1);

    panel.clear();
    QCOMPARE(panel.rowCount(), 0);
    QVERIFY(panel.issues().empty());
    QCOMPARE(cancelled.count(), 0);
  }
};

int runGeometryCheckPanelTests(int argc, char** argv) {
  GeometryCheckPanelTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_geometry_check_panel.moc"
