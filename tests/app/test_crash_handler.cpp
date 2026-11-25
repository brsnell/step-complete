// The crash handler (docs/designs/crash-reporting.md), end to end: a helper program installs it
// and crashes; the report it leaves names the cause and has the context and a stack.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

class CrashHandlerTest : public QObject {
  Q_OBJECT

private:
  // Runs the helper in a fresh folder and returns the one report it wrote.
  QString crash(const QString& mode, QString* folder, QTemporaryDir& dir) {
    *folder = dir.filePath(QStringLiteral("crashes"));
    QProcess helper;
    helper.setProcessChannelMode(QProcess::ForwardedChannels);
    helper.start(QStringLiteral(SC_CRASH_HELPER), {*folder, mode});
    if (!helper.waitForFinished(60000)) {
      helper.kill();
      return {};
    }
    // Ended abnormally: a signal, or a non-zero code (Windows exception codes, abort()).
    if (helper.exitStatus() == QProcess::NormalExit && helper.exitCode() == 0) {
      return {};
    }
    const QStringList reports =
        QDir(*folder).entryList({QStringLiteral("crash-*.txt")}, QDir::Files, QDir::Name);
    if (reports.size() != 1) {
      return {};
    }
    QFile file(QDir(*folder).filePath(reports.front()));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
  }

  void checkCommon(const QString& report, const QString& folder) {
    QVERIFY2(report.startsWith(QStringLiteral("STEP Complete crash report")), qPrintable(report));
    QVERIFY(report.contains(QStringLiteral("Time (UTC): ")));
    QVERIFY(report.contains(QStringLiteral("Uptime: ")));
    QVERIFY(report.contains(QStringLiteral("Version: crash helper")));
    QVERIFY(report.contains(QStringLiteral("Model: STEP, 1.0 MB, 5 part instances")));
    QVERIFY(report.contains(QStringLiteral("Activity: testing")));
    const qsizetype stack = report.indexOf(QStringLiteral("\nStack"));
    QVERIFY2(stack > 0, qPrintable(report));
    QVERIFY2(report.mid(stack).count(QLatin1Char('\n')) >= 4, qPrintable(report)); // several frames
#if defined(_WIN32)
    const QStringList dumps = QDir(folder).entryList({QStringLiteral("crash-*.dmp")}, QDir::Files);
    QCOMPARE(dumps.size(), 1);
    QVERIFY(QFileInfo(QDir(folder).filePath(dumps.front())).size() > 1000);
#else
    Q_UNUSED(folder);
#endif
  }

private Q_SLOTS:
  void nullPointerWrite() {
    QTemporaryDir dir;
    QString folder;
    const QString report = crash(QStringLiteral("segv"), &folder, dir);
#if defined(_WIN32)
    QVERIFY2(report.contains(QStringLiteral("Cause: access violation")), qPrintable(report));
#elif defined(__APPLE__)
    // A bad access is SIGSEGV or SIGBUS on macOS, depending on the address.
    QVERIFY2(report.contains(QStringLiteral("Cause: SIGSEGV")) ||
                 report.contains(QStringLiteral("Cause: SIGBUS")),
             qPrintable(report));
#else
    QVERIFY2(report.contains(QStringLiteral("Cause: SIGSEGV")), qPrintable(report));
#endif
    checkCommon(report, folder);
  }

  void abortCall() {
    QTemporaryDir dir;
    QString folder;
    const QString report = crash(QStringLiteral("abort"), &folder, dir);
#if defined(_WIN32)
    QVERIFY2(report.contains(QStringLiteral("Cause: abort()")), qPrintable(report));
#else
    QVERIFY2(report.contains(QStringLiteral("Cause: SIGABRT")), qPrintable(report));
#endif
    checkCommon(report, folder);
  }

  void uncaughtException() {
    QTemporaryDir dir;
    QString folder;
    const QString report = crash(QStringLiteral("exception"), &folder, dir);
#if defined(_WIN32)
    // MSVC's runtime gives std::terminate no current exception, so only the kind is known.
    QVERIFY2(report.contains(QStringLiteral("Message: Uncaught C++ exception")), qPrintable(report));
#else
    QVERIFY2(report.contains(QStringLiteral("Message: Uncaught exception: helper exception")),
             qPrintable(report));
#endif
    checkCommon(report, folder);
  }

  void qtFatalMessage() {
    QTemporaryDir dir;
    QString folder;
    const QString report = crash(QStringLiteral("fatal"), &folder, dir);
    QVERIFY2(report.contains(QStringLiteral("Message: Qt fatal message: helper fatal 42")),
             qPrintable(report));
    checkCommon(report, folder);
  }
};

QTEST_GUILESS_MAIN(CrashHandlerTest)
#include "test_crash_handler.moc"
