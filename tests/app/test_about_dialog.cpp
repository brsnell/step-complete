// AboutDialog: license texts load from resources, the system report has the
// versions, and the copy button fills the clipboard.

#include "app/AboutDialog.h"
#include "core/Version.h"

#include <QApplication>
#include <QClipboard>
#include <QPushButton>
#include <QTest>

using sc::app::AboutDialog;

class AboutDialogTest : public QObject {
  Q_OBJECT
private Q_SLOTS:
  void licenseTextsLoad() {
    const auto entries = AboutDialog::licenseEntries();
    QVERIFY(entries.size() >= 7);
    for (const auto& entry : entries) {
      QVERIFY2(!entry.title.isEmpty(), "entry without a title");
      QVERIFY2(!entry.sourceUrl.isEmpty(), qPrintable(entry.title));
      QVERIFY2(!entry.files.isEmpty(), qPrintable(entry.title));
      QVERIFY2(AboutDialog::licenseText(entry).size() > 200, qPrintable(entry.title));
    }
  }

  void listShowsText() {
    AboutDialog dialog;
    QCOMPARE(dialog.licenseCount(), int(AboutDialog::licenseEntries().size()));
    for (int i = 0; i < dialog.licenseCount(); ++i) {
      dialog.selectLicense(i);
      QVERIFY2(dialog.shownLicenseText().size() > 200, qPrintable(QString::number(i)));
    }
  }

  void systemInfoHasVersions() {
    AboutDialog dialog;
    dialog.setGraphicsInfo(QStringLiteral("GLvendor: Test Vendor\nGLrenderer: Test Renderer"));
    const QString info = dialog.systemInfo();
    QVERIFY(info.contains(QString::fromUtf8(sc::core::kAppVersion)));
    QVERIFY(info.contains(QString::fromLatin1(qVersion())));
    QVERIFY(info.contains(QStringLiteral(QT_VERSION_STR)));
    QVERIFY(info.contains(QStringLiteral("Open CASCADE")));
    QVERIFY(info.contains(QStringLiteral("Test Renderer")));
  }

  void copyButtonFillsClipboard() {
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) {
      QTest::qSkip("no clipboard on this platform", __FILE__, __LINE__);
      return;
    }
    clipboard->setText(QStringLiteral("sentinel"));
    if (clipboard->text() != QStringLiteral("sentinel")) {
      QTest::qSkip("clipboard unusable on this platform", __FILE__, __LINE__);
      return;
    }
    AboutDialog dialog;
    auto* button = dialog.findChild<QPushButton*>(QStringLiteral("copySystemInfoButton"));
    QVERIFY(button != nullptr);
    button->click();
    QCOMPARE(clipboard->text(), dialog.systemInfo());
  }
};

int runAboutDialogTests(int argc, char** argv) {
  AboutDialogTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_about_dialog.moc"
