// The crash handler (docs/designs/crash-reporting.md), end to end: a helper program installs it
// and crashes; the report it leaves names the cause and has the context and a stack.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>

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
      qWarning("helper %s did not finish: %s", qPrintable(mode), qPrintable(helper.errorString()));
      helper.kill();
      return {};
    }
    const QStringList files = QDir(*folder).entryList(QDir::Files);
    qInfo("helper %s: exit status %d, code %lld (0x%llx), files: %s", qPrintable(mode),
          static_cast<int>(helper.exitStatus()), static_cast<long long>(helper.exitCode()),
          static_cast<unsigned long long>(static_cast<unsigned int>(helper.exitCode())),
          qPrintable(files.join(QStringLiteral(", "))));
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
    QVERIFY2(report.contains(QStringLiteral("Message: Uncaught exception: helper exception")),
             qPrintable(report));
    checkCommon(report, folder);
  }

  // Value: protects=a report after a stack overflow on the main thread; fails_when=installCrashHandler
  // stops giving the main thread its alternate signal stack (Windows: its stack guarantee);
  // why_new=the other overflow test runs on a worker thread; seam=none; no_claim=OCCT's pool threads.
  void mainStackOverflow() {
    QTemporaryDir dir;
    QString folder;
    const QString report = crash(QStringLiteral("overflow"), &folder, dir);
#if defined(_WIN32)
    QVERIFY2(report.contains(QStringLiteral("Cause: stack overflow")), qPrintable(report));
#else
    QVERIFY2(report.contains(QStringLiteral("Cause: SIGSEGV")) ||
                 report.contains(QStringLiteral("Cause: SIGBUS")),
             qPrintable(report));
#endif
    checkCommon(report, folder);
  }

  // A stack overflow on a prepared worker thread: the handler needs stack of its own there.
  void workerStackOverflow() {
    QTemporaryDir dir;
    QString folder;
    const QString report = crash(QStringLiteral("thread-overflow"), &folder, dir);
#if defined(_WIN32)
    QVERIFY2(report.contains(QStringLiteral("Cause: stack overflow")), qPrintable(report));
#else
    // SIGSEGV, or SIGBUS on macOS, depending on where the guard page is hit.
    QVERIFY2(report.contains(QStringLiteral("Cause: SIGSEGV")) ||
                 report.contains(QStringLiteral("Cause: SIGBUS")),
             qPrintable(report));
#endif
    checkCommon(report, folder);
  }

  // An uncaught exception on a prepared worker thread keeps its text (MSVC keeps the terminate
  // handler per thread).
  void workerException() {
    QTemporaryDir dir;
    QString folder;
    const QString report = crash(QStringLiteral("thread-exception"), &folder, dir);
    QVERIFY2(report.contains(QStringLiteral("Message: Uncaught exception: worker exception")),
             qPrintable(report));
    checkCommon(report, folder);
  }

  // Two threads fault together: one complete report, not one cut short by the other.
  void twoThreadsAtOnce() {
    QTemporaryDir dir;
    QString folder;
    const QString report = crash(QStringLiteral("two-threads"), &folder, dir);
#if defined(_WIN32)
    QVERIFY2(report.contains(QStringLiteral("Cause: access violation")), qPrintable(report));
    QVERIFY2(report.contains(QStringLiteral("minidump with the same name")),
             qPrintable(report)); // the last line
#else
    QVERIFY2(report.contains(QStringLiteral("Cause: SIGSEGV")) ||
                 report.contains(QStringLiteral("Cause: SIGBUS")),
             qPrintable(report));
    // Value: protects=a complete stack when two threads fault at once; fails_when=the second
    // thread ends the process while the first is still writing (the stack is cut part-way);
    // why_new=checkCommon accepts any four lines of stack; seam=none; no_claim=which thread's
    // stack is reported.
    // Complete: as many frames as the same code faulting on one thread, and a whole last line.
    QTemporaryDir referenceDir;
    QString referenceFolder;
    const QString reference = crash(QStringLiteral("thread-segv"), &referenceFolder, referenceDir);
    const auto stackLines = [](const QString& text) {
      const qsizetype stack = text.indexOf(QStringLiteral("\nStack"));
      return stack < 0 ? -1 : text.mid(stack).count(QLatin1Char('\n'));
    };
    QVERIFY2(stackLines(reference) >= 4, qPrintable(reference));
    QVERIFY2(report.endsWith(QLatin1Char('\n')), qPrintable(report));
    QCOMPARE(stackLines(report), stackLines(reference));
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

int main(int argc, char** argv) {
  // Keep the output readable when something goes wrong (as app_test_main does): unbuffered
  // stdout survives an abnormal end, and Qt's own messages go to stderr instead of a debugger.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (qEnvironmentVariableIsEmpty("QT_FORCE_STDERR_LOGGING")) {
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
  }
  std::fprintf(stderr, "crash handler test: helper %s\n", SC_CRASH_HELPER);
  QCoreApplication app(argc, argv);
  CrashHandlerTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_crash_handler.moc"
