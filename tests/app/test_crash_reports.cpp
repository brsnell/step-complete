// Crash reports (docs/designs/crash-reporting.md): the folder's reports, the last one shown,
// pruning, the GitHub issue link and the dialog shown on the next start.

#include "app/CrashReports.h"

#include <QDir>
#include <QFile>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

using sc::app::CrashReportDialog;
using sc::app::CrashReports;

namespace {

void writeFile(const QString& path, const QByteArray& content) {
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(content);
}

const QByteArray SampleReport =
    "STEP Complete crash report\n\nTime (UTC): 2026-11-25 10:00:00\nUptime: 42 s\n"
    "Cause: SIGSEGV (segmentation fault) at address 0x0\n\nVersion: STEP Complete 1.3.0\n"
    "Model: STEP, 12.0 MB, 40 part instances, 12 unique parts\n\nStack:\nframe 1\nframe 2\n";

} // namespace

class CrashReportsTest : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void newReportsAndLastSeen() {
    QTemporaryDir dir;
    CrashReports reports(dir.path());
    QVERIFY(reports.reports().isEmpty());
    QVERIFY(reports.newReports().isEmpty());

    writeFile(dir.filePath(QStringLiteral("crash-20261124-090000.txt")), SampleReport);
    writeFile(dir.filePath(QStringLiteral("crash-20261125-100000.txt")), SampleReport);
    writeFile(dir.filePath(QStringLiteral("notes.txt")), "not a report");
    QCOMPARE(reports.reports().size(), 2);
    QVERIFY(reports.reports().front().endsWith(QStringLiteral("crash-20261125-100000.txt"))); // newest first
    QCOMPARE(reports.newReports().size(), 2);

    reports.markSeen(reports.reports().front());
    QVERIFY(reports.newReports().isEmpty()); // the older one counts as seen too
    reports.markSeen(dir.filePath(QStringLiteral("crash-20261124-090000.txt"))); // never goes back
    QVERIFY(reports.newReports().isEmpty());

    writeFile(dir.filePath(QStringLiteral("crash-20261125-110000.txt")), SampleReport);
    QCOMPARE(reports.newReports().size(), 1);
    QCOMPARE(CrashReports(dir.path()).newReports().size(), 1); // kept on disk
  }

  void pruneKeepsTheNewest() {
    QTemporaryDir dir;
    for (int i = 0; i < 12; ++i) {
      const QString base = dir.filePath(QStringLiteral("crash-202611%1-120000").arg(10 + i));
      writeFile(base + QStringLiteral(".txt"), SampleReport);
      writeFile(base + QStringLiteral(".dmp"), "MDMP");
    }
    CrashReports reports(dir.path());
    QCOMPARE(reports.reports().size(), 12);
    QVERIFY(!CrashReports::dumpFor(reports.reports().back()).isEmpty());
    reports.prune(10);
    QCOMPARE(reports.reports().size(), 10);
    QVERIFY(reports.reports().back().endsWith(QStringLiteral("crash-20261112-120000.txt")));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("crash-20261110-120000.dmp"))));
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("crash-20261112-120000.dmp"))));
    QVERIFY(CrashReports::dumpFor(dir.filePath(QStringLiteral("crash-20261199-000000.txt"))).isEmpty());
  }

  void issueLink() {
    const QUrl url = CrashReports::issueUrl(QString::fromUtf8(SampleReport), true);
    QVERIFY(url.isValid());
    QCOMPARE(url.host(), QStringLiteral("github.com"));
    QCOMPARE(url.path(), QStringLiteral("/brsnell/step-complete/issues/new"));
    const QUrlQuery query(url);
    const QString title = query.queryItemValue(QStringLiteral("title"), QUrl::FullyDecoded);
    QVERIFY2(title.endsWith(QStringLiteral(": SIGSEGV (segmentation fault)")), qPrintable(title));
    QVERIFY(title.startsWith(QStringLiteral("Crash in STEP Complete ")));
    const QString body = query.queryItemValue(QStringLiteral("body"), QUrl::FullyDecoded);
    QVERIFY(body.contains(QStringLiteral("Model: STEP, 12.0 MB")));
    QVERIFY(body.contains(QStringLiteral("```\nSTEP Complete crash report")));
    QVERIFY(body.contains(QStringLiteral("attach the minidump")));
    QVERIFY(body.contains(QStringLiteral("small pieces of the open model")));
    QVERIFY(!CrashReports::issueUrl(QString::fromUtf8(SampleReport), false)
                 .toString()
                 .contains(QStringLiteral("minidump")));

    // A short report is whole.
    QVERIFY(!body.contains(QStringLiteral("cut to fit the link")));

    // A long one is cut so the encoded link fits, by whole lines from the end, and says so. Linux
    // stack lines are mostly characters that encode to three bytes each ('/', '(', ' ', ...).
    QString longReport = QString::fromUtf8(SampleReport);
    for (int i = 0; i < 64; ++i) {
      longReport +=
          QStringLiteral("/opt/StepComplete/usr/lib/libTKernel.so.8.0(_ZN15Standard_Failure5RaiseEv+0x%1) "
                         "[0x7f%2]\n")
              .arg(i, 4, 16, QLatin1Char('0'))
              .arg(i, 10, 16, QLatin1Char('0'));
    }
    QVERIFY(longReport.size() < 8000); // fits as text, not once encoded
    const QUrl longUrl = CrashReports::issueUrl(longReport, false);
    QVERIFY2(longUrl.toEncoded().size() <= CrashReports::MaxIssueUrl,
             qPrintable(QString::number(longUrl.toEncoded().size())));
    const QString cut = QUrlQuery(longUrl).queryItemValue(QStringLiteral("body"), QUrl::FullyDecoded);
    QVERIFY(cut.contains(QStringLiteral("Cause: SIGSEGV")));
    QVERIFY(cut.contains(QStringLiteral("libTKernel")));                  // some of the stack is kept
    QVERIFY(cut.contains(QStringLiteral("]\n[... cut to fit the link"))); // after a whole line
    QVERIFY(cut.endsWith(QStringLiteral("```\n")));
  }

  // Value: protects=the issue link for a report that is one long line (nothing to cut by lines);
  // fails_when=the cut loop leaves an over-long, invalid or unfenced link once every line is
  // dropped; why_new=issueLink only cuts many short lines; seam=none; no_claim=that the header
  // lines survive (the one line is dropped whole).
  void issueLinkOneLongLine() {
    QString oneLine;
    for (int i = 0; i < 4000; ++i) {
      oneLine += QStringLiteral("a/ ");
    }
    const QUrl url = CrashReports::issueUrl(oneLine, false);
    QVERIFY(url.isValid());
    QVERIFY2(url.toEncoded().size() <= CrashReports::MaxIssueUrl,
             qPrintable(QString::number(url.toEncoded().size())));
    const QString body = QUrlQuery(url).queryItemValue(QStringLiteral("body"), QUrl::FullyDecoded);
    QVERIFY2(body.contains(QStringLiteral("cut to fit the link")), qPrintable(body));
    QVERIFY2(body.endsWith(QStringLiteral("```\n")), qPrintable(body));
  }

  void dialog() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("crash-20261125-100000.txt"));
    writeFile(path, SampleReport);
    CrashReportDialog dialog(path, 2);
    auto* text = dialog.findChild<QPlainTextEdit*>(QStringLiteral("crashReportText"));
    QVERIFY(text);
    QVERIFY(text->isReadOnly());
    QCOMPARE(text->toPlainText(), QString::fromUtf8(SampleReport));
    QCOMPARE(dialog.reportText(), QString::fromUtf8(SampleReport));
    for (const char* name : {"crashReportGitHub", "crashReportCopy", "crashReportFolder"}) {
      QVERIFY2(dialog.findChild<QPushButton*>(QString::fromLatin1(name)), name);
    }
    QVERIFY(!dialog.issueUrl().toString().contains(QStringLiteral("minidump"))); // no .dmp beside it
    writeFile(dir.filePath(QStringLiteral("crash-20261125-100000.dmp")), "MDMP");
    QVERIFY(QUrlQuery(dialog.issueUrl())
                .queryItemValue(QStringLiteral("body"), QUrl::FullyDecoded)
                .contains(QStringLiteral("minidump")));
  }
};

int runCrashReportsTests(int argc, char** argv) {
  CrashReportsTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_crash_reports.moc"
