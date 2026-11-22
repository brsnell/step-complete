// User saved views (docs/designs/user-saved-views.md): the JSON form, the per-file store and the
// Manage Saved Views dialog.

#include "app/SavedViewsDialog.h"
#include "app/UserViews.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using sc::app::ModelDataStore;
using sc::app::SavedViewsDialog;
using sc::app::UserView;

namespace {

UserView sampleView(const QString& name) {
  UserView view;
  view.name = name;
  view.camera.eye = gp_Pnt(100, -200, 300);
  view.camera.center = gp_Pnt(10, 20, 30);
  view.camera.up = gp_Dir(0, 0, 1);
  view.camera.perspective = true;
  view.camera.scale = 123.5;
  view.camera.fieldOfView = 30.0;
  view.hiddenParts = {QStringLiteral("0:1:1:4/0:1:1:4:2"), QStringLiteral("0:1:1:4/0:1:1:4:3")};
  view.sections[1].enabled = true;
  view.sections[1].normal = gp_Dir(1, 1, 0);
  view.sections[1].offset = -12.25;
  view.sections[1].capping = false;
  view.sections[1].hatch = false;
  return view;
}

void writeFile(const QString& path, const QByteArray& content) {
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(content);
}

QJsonObject viewsData(const QString& name) {
  return QJsonObject{{QStringLiteral("views"), sc::app::toJson(std::vector<UserView>{sampleView(name)})}};
}

QString firstViewName(const QJsonObject& data) {
  const std::vector<UserView> views =
      sc::app::userViewsFromJson(data.value(QStringLiteral("views")).toArray());
  return views.empty() ? QString() : views.front().name;
}

} // namespace

class UserViewsTest : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void viewRoundTripsThroughJson() {
    const UserView view = sampleView(QStringLiteral("Front flange"));
    const QJsonObject json = sc::app::toJson(view);
    // Through text, as on disk.
    const QJsonObject reread = QJsonDocument::fromJson(QJsonDocument(json).toJson()).object();
    const std::optional<UserView> back = sc::app::userViewFromJson(reread);
    QVERIFY(back.has_value());
    QCOMPARE(back->name, view.name);
    QVERIFY(back->camera.eye.IsEqual(view.camera.eye, 1e-12));
    QVERIFY(back->camera.center.IsEqual(view.camera.center, 1e-12));
    QVERIFY(back->camera.up.IsEqual(view.camera.up, 1e-12));
    QCOMPARE(back->camera.perspective, true);
    QCOMPARE(back->camera.scale, 123.5);
    QCOMPARE(back->camera.fieldOfView, 30.0);
    QCOMPARE(back->hiddenParts, view.hiddenParts);
    QVERIFY(!back->sections[0].enabled);
    QVERIFY(back->sections[1].enabled);
    QVERIFY(back->sections[1].normal.IsEqual(gp_Dir(1, 1, 0), 1e-12));
    QCOMPARE(back->sections[1].offset, -12.25);
    QVERIFY(!back->sections[1].capping);
    QVERIFY(!back->sections[1].hatch);
  }

  void unusableEntriesAreSkipped() {
    QJsonObject noName = sc::app::toJson(sampleView(QStringLiteral("x")));
    noName.insert(QStringLiteral("name"), QStringLiteral("   "));
    QJsonObject noCamera = sc::app::toJson(sampleView(QStringLiteral("y")));
    noCamera.remove(QStringLiteral("camera"));
    QJsonObject sameEyeAndCenter = sc::app::toJson(sampleView(QStringLiteral("z")));
    QJsonObject camera = sameEyeAndCenter.value(QStringLiteral("camera")).toObject();
    camera.insert(QStringLiteral("eye"), camera.value(QStringLiteral("center")));
    sameEyeAndCenter.insert(QStringLiteral("camera"), camera);
    const QJsonArray array{noName, noCamera, sameEyeAndCenter, QStringLiteral("not an object"),
                           sc::app::toJson(sampleView(QStringLiteral("Kept")))};
    const std::vector<UserView> views = sc::app::userViewsFromJson(array);
    QCOMPARE(views.size(), size_t(1));
    QCOMPARE(views.front().name, QStringLiteral("Kept"));
  }

  void storeFindsDataByContent() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ModelDataStore store(dir.filePath(QStringLiteral("store")));
    const QString model = dir.filePath(QStringLiteral("bracket.stp"));
    writeFile(model, QByteArray("ISO-10303-21; bracket"));

    QVERIFY(store.load(model).data.isEmpty()); // nothing saved yet
    QString error;
    QVERIFY2(store.save(model, viewsData(QStringLiteral("Top")), &error), qPrintable(error));
    QCOMPARE(firstViewName(store.load(model).data), QStringLiteral("Top"));

    // A renamed copy elsewhere (a fresh store, as in a later session) finds the same views.
    const QString copy = dir.filePath(QStringLiteral("elsewhere/bracket renamed.stp"));
    QDir().mkpath(QFileInfo(copy).absolutePath());
    QVERIFY(QFile::copy(model, copy));
    ModelDataStore later(store.directory());
    QCOMPARE(firstViewName(later.load(copy).data), QStringLiteral("Top"));
  }

  void anEditedFileKeepsItsData() {
    QTemporaryDir dir;
    ModelDataStore store(dir.filePath(QStringLiteral("store")));
    const QString model = dir.filePath(QStringLiteral("housing.stp"));
    writeFile(model, QByteArray("ISO-10303-21; first export"));
    QVERIFY(store.save(model, viewsData(QStringLiteral("Inside"))));
    const QStringList before = QDir(store.directory()).entryList({QStringLiteral("*.json")}, QDir::Files);

    // Re-exported in place: other content, same path. A new session finds the views by path,
    // and moves them to the new content's document.
    writeFile(model, QByteArray("ISO-10303-21; second export, a little longer"));
    ModelDataStore later(store.directory());
    QCOMPARE(firstViewName(later.load(model).data), QStringLiteral("Inside"));
    const QStringList after = QDir(store.directory()).entryList({QStringLiteral("*.json")}, QDir::Files);
    QCOMPARE(after.size(), before.size()); // index.json and one model document
    QVERIFY(after != before);              // ... under the new fingerprint
    ModelDataStore third(store.directory());
    QCOMPARE(firstViewName(third.load(model).data), QStringLiteral("Inside"));
  }

  void anUnreadableDocumentIsReportedAndKept() {
    QTemporaryDir dir;
    ModelDataStore store(dir.filePath(QStringLiteral("store")));
    const QString model = dir.filePath(QStringLiteral("plate.stp"));
    writeFile(model, QByteArray("ISO-10303-21; plate"));
    QVERIFY(store.save(model, viewsData(QStringLiteral("Old"))));
    QString document;
    for (const QString& name : QDir(store.directory()).entryList({QStringLiteral("*.json")}, QDir::Files)) {
      if (name != QLatin1String("index.json")) {
        document = QDir(store.directory()).filePath(name);
      }
    }
    QVERIFY(!document.isEmpty());
    writeFile(document, QByteArray("{ not json"));

    const ModelDataStore::Loaded loaded = store.load(model);
    QVERIFY(!loaded.ok);
    QVERIFY(!loaded.error.isEmpty());
    QVERIFY(loaded.data.isEmpty());

    // The next save keeps the unreadable file beside the new one.
    QVERIFY(store.save(model, viewsData(QStringLiteral("New"))));
    QVERIFY(QFile::exists(document + QStringLiteral(".unreadable")));
    QCOMPARE(firstViewName(store.load(model).data), QStringLiteral("New"));
  }

  void dialogRenamesDeletesAndReorders() {
    SavedViewsDialog dialog(
        {sampleView(QStringLiteral("A")), sampleView(QStringLiteral("B")), sampleView(QStringLiteral("C"))});
    const auto names = [&dialog] {
      QStringList out;
      for (const UserView& view : dialog.views()) {
        out << view.name;
      }
      return out;
    };
    dialog.setCurrentRow(1);
    dialog.renameCurrent(QStringLiteral("  Bottom  "));
    QCOMPARE(names(), (QStringList{"A", "Bottom", "C"})); // trimmed
    dialog.renameCurrent(QStringLiteral("   "));
    QCOMPARE(names(), (QStringList{"A", "Bottom", "C"})); // empty refused
    dialog.renameCurrent(QStringLiteral("c"));
    QCOMPARE(names(), (QStringList{"A", "Bottom", "C"})); // taken (ignoring case) refused

    dialog.moveCurrent(1);
    QCOMPARE(names(), (QStringList{"A", "C", "Bottom"}));
    dialog.moveCurrent(1); // already last
    QCOMPARE(names(), (QStringList{"A", "C", "Bottom"}));
    dialog.setCurrentRow(0);
    dialog.deleteCurrent();
    QCOMPARE(names(), (QStringList{"C", "Bottom"}));
    QCOMPARE(dialog.views().back().hiddenParts.size(), 2); // the rest of a view travels with it
  }
};

int runUserViewsTests(int argc, char** argv) {
  UserViewsTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_user_views.moc"
