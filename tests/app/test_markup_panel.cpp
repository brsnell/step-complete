// Markup (docs/designs/markup.md): the JSON form kept in the per-model store, the Export Markup
// file, and the Markup panel.

#include "app/MarkupJson.h"
#include "app/MarkupPanel.h"

#include <QJsonArray>
#include <QListWidget>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

using sc::app::MarkupPanel;
using sc::core::Markup;

namespace {

Markup sample() {
  Markup markup;
  markup.addNote({0, "Burr on this edge\nremove before paint", "0:1:1:1/0:1:1:2", gp_Pnt(1.5, -2, 30)});
  markup.addNote({0, "Check the fit", "", gp_Pnt(0, 0, 0)});
  markup.addStroke({0, "0:1:1:1/0:1:1:3", {gp_Pnt(0, 0, 0), gp_Pnt(1, 2, 3), gp_Pnt(4, 5, 6.25)}});
  markup.addStroke({0, "", {gp_Pnt(-1, -1, -1), gp_Pnt(1, 1, 1)}});
  return markup;
}

} // namespace

class MarkupPanelTest : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void jsonRoundTrip() {
    const Markup markup = sample();
    const Markup back = sc::app::markupFromJson(sc::app::toJson(markup));
    QCOMPARE(back.notes().size(), size_t(2));
    QCOMPARE(back.strokes().size(), size_t(2));
    QCOMPARE(back.notes()[0].text, std::string("Burr on this edge\nremove before paint"));
    QCOMPARE(back.notes()[0].part, std::string("0:1:1:1/0:1:1:2"));
    QVERIFY(back.notes()[0].point.IsEqual(gp_Pnt(1.5, -2, 30), 0.0));
    QVERIFY(back.notes()[1].part.empty());
    QCOMPARE(back.strokes()[0].points.size(), size_t(3));
    QVERIFY(back.strokes()[0].points[2].IsEqual(gp_Pnt(4, 5, 6.25), 0.0));
    QVERIFY(back.strokes()[1].part.empty());
  }

  void malformedEntriesAreSkipped() {
    QJsonObject object = sc::app::toJson(sample());
    QJsonArray notes = object.value(QStringLiteral("notes")).toArray();
    notes.append(QJsonObject{{QStringLiteral("text"), QStringLiteral("  ")},
                             {QStringLiteral("point"), QJsonArray{0, 0, 0}}}); // no text
    notes.append(QJsonObject{{QStringLiteral("text"), QStringLiteral("x")},
                             {QStringLiteral("point"), QJsonArray{0, 0}}}); // short point
    notes.append(QStringLiteral("not an object"));
    object.insert(QStringLiteral("notes"), notes);
    QJsonArray strokes = object.value(QStringLiteral("strokes")).toArray();
    strokes.append(QJsonObject{{QStringLiteral("points"), QJsonArray{0, 0, 0}}});            // one point
    strokes.append(QJsonObject{{QStringLiteral("points"), QJsonArray{0, 0, 0, 1, 1}}});      // not whole
    strokes.append(QJsonObject{{QStringLiteral("points"), QJsonArray{0, 0, 0, 1, "a", 1}}}); // not numbers
    object.insert(QStringLiteral("strokes"), strokes);
    const Markup back = sc::app::markupFromJson(object);
    QCOMPARE(back.notes().size(), size_t(2));
    QCOMPARE(back.strokes().size(), size_t(2));
    QVERIFY(sc::app::markupFromJson(QJsonObject()).empty());
  }

  void exportFile() {
    const QJsonObject document = sc::app::markupFileDocument(sample(), QStringLiteral("bracket.step"));
    QString error;
    const std::optional<sc::app::MarkupFile> file = sc::app::markupFromFileDocument(document, &error);
    QVERIFY(file);
    QCOMPARE(file->modelName, QStringLiteral("bracket.step"));
    QCOMPARE(file->markup.size(), 4);

    QVERIFY(!sc::app::markupFromFileDocument(QJsonObject{{QStringLiteral("notes"), QJsonArray()}}, &error));
    QVERIFY(error.contains(QStringLiteral("not a STEP Complete markup file")));
    QJsonObject newer = document;
    newer.insert(QStringLiteral("version"), 99);
    QVERIFY(!sc::app::markupFromFileDocument(newer, &error));
    QVERIFY(error.contains(QStringLiteral("newer version")));
  }

  void panelRows() {
    MarkupPanel panel;
    auto* hint = panel.findChild<QWidget*>(QStringLiteral("markupHint"));
    auto* remove = panel.findChild<QToolButton*>(QStringLiteral("markupDelete"));
    QVERIFY(hint && remove);
    QVERIFY(!hint->isHidden());
    QCOMPARE(panel.rowCount(), 0);

    const Markup markup = sample();
    const int lost = markup.strokes()[0].id;
    panel.setMarkup(markup, [lost](int id) { return id == lost; });
    QVERIFY(hint->isHidden());
    QCOMPARE(panel.rowCount(), 4);
    QCOMPARE(panel.rowText(0), QStringLiteral("Burr on this edge ..."));
    QCOMPARE(panel.rowText(1), QStringLiteral("Check the fit"));
    QCOMPARE(panel.rowText(2), QStringLiteral("Sketch 1 (part missing)"));
    QCOMPARE(panel.rowText(3), QStringLiteral("Sketch 2"));
    QCOMPARE(panel.idAt(1), markup.notes()[1].id);
    QCOMPARE(panel.currentId(), 0);
    QVERIFY(!remove->isEnabled());

    // Choosing, and keeping the choice across a refresh.
    QSignalSpy chosen(&panel, &MarkupPanel::itemChosen);
    panel.setCurrentId(markup.strokes()[1].id);
    QCOMPARE(chosen.count(), 1);
    QCOMPARE(chosen.last().at(0).toInt(), markup.strokes()[1].id);
    QVERIFY(remove->isEnabled());
    panel.setMarkup(markup, {});
    QCOMPARE(panel.currentId(), markup.strokes()[1].id);
    QCOMPARE(panel.rowText(2), QStringLiteral("Sketch 1"));

    // Delete asks for the chosen rows.
    QSignalSpy deleted(&panel, &MarkupPanel::deleteRequested);
    QTest::mouseClick(remove, Qt::LeftButton);
    QCOMPARE(deleted.count(), 1);
    QCOMPARE(panel.selectedIds(), std::vector<int>{markup.strokes()[1].id});

    // Double-click (activation) names the item.
    QSignalSpy activated(&panel, &MarkupPanel::itemActivated);
    auto* list = panel.findChild<QListWidget*>(QStringLiteral("markupList"));
    QVERIFY(list);
    Q_EMIT list->itemActivated(list->item(0));
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.last().at(0).toInt(), markup.notes()[0].id);

    panel.clear();
    QCOMPARE(panel.rowCount(), 0);
    QVERIFY(!hint->isHidden());
    QCOMPARE(panel.currentId(), 0);
  }

  void noteLabels() {
    QCOMPARE(MarkupPanel::noteLabel("  one line  "), QStringLiteral("one line"));
    QCOMPARE(MarkupPanel::noteLabel("first\nsecond"), QStringLiteral("first ..."));
  }
};

int runMarkupPanelTests(int argc, char** argv) {
  MarkupPanelTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_markup_panel.moc"
