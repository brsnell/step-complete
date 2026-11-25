// Crashes on purpose for app.crash_handler (docs/designs/crash-reporting.md):
//   sc_crash_helper <report folder> <segv|abort|exception|fatal>

#include "app/CrashHandler.h"

#include <QCoreApplication>
#include <QString>

#include <cstdlib>
#include <cstring>
#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#endif

int main(int argc, char** argv) {
  if (argc < 3) {
    return 2;
  }
#if defined(_WIN32)
  // No "stopped working" dialog to wait for on a CI runner.
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
  QCoreApplication app(argc, argv);
  sc::app::setCrashEnvironment(QStringLiteral("Version: crash helper\n"));
  sc::app::setCrashContext(sc::app::CrashContext::Model, QStringLiteral("STEP, 1.0 MB, 5 part instances"));
  sc::app::setCrashContext(sc::app::CrashContext::Activity, QStringLiteral("testing"));
  if (!sc::app::installCrashHandler(QString::fromLocal8Bit(argv[1]))) {
    return 3;
  }
  const char* mode = argv[2];
  if (std::strcmp(mode, "segv") == 0) {
    volatile int* volatile target = nullptr;
    *target = 42;
  } else if (std::strcmp(mode, "abort") == 0) {
    std::abort();
  } else if (std::strcmp(mode, "exception") == 0) {
    throw std::runtime_error("helper exception");
  } else if (std::strcmp(mode, "fatal") == 0) {
    qFatal("helper fatal %d", 42);
  }
  return 0;
}
