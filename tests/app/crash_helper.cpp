// Crashes on purpose for app.crash_handler (docs/designs/crash-reporting.md):
//   sc_crash_helper <report folder> <segv|abort|exception|fatal|thread-overflow|thread-exception|two-threads>

#include "app/CrashHandler.h"

#include <QCoreApplication>
#include <QString>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

volatile bool s_stop = false; // never set: keeps the compiler from proving the recursion endless

// Recurses until the thread's stack runs out; the local array keeps each frame large.
int recurse(int depth) {
  volatile char frame[1024];
  frame[0] = static_cast<char>(depth);
  if (s_stop) {
    return frame[0];
  }
  return recurse(depth + 1) + frame[0];
}

void writeNull() {
  volatile int* volatile target = nullptr;
  *target = 42;
}

// Runs 'work' on a new thread prepared like the app's job threads, and waits for it.
template <typename Work>
void onPreparedThread(Work work) {
  std::thread thread([work] {
    sc::app::prepareCrashHandlerThread();
    work();
  });
  thread.join();
}

} // namespace

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
    writeNull();
  } else if (std::strcmp(mode, "abort") == 0) {
    std::abort();
  } else if (std::strcmp(mode, "exception") == 0) {
    throw std::runtime_error("helper exception");
  } else if (std::strcmp(mode, "fatal") == 0) {
    qFatal("helper fatal %d", 42);
  } else if (std::strcmp(mode, "thread-overflow") == 0) {
    onPreparedThread([] { recurse(0); });
  } else if (std::strcmp(mode, "thread-exception") == 0) {
    onPreparedThread([] { throw std::runtime_error("worker exception"); });
  } else if (std::strcmp(mode, "two-threads") == 0) {
    // Both threads fault as close together as the scheduler allows.
    std::atomic<int> ready{0};
    const auto crash = [&ready] {
      sc::app::prepareCrashHandlerThread();
      ready.fetch_add(1);
      while (ready.load() < 2) {
      }
      writeNull();
    };
    std::thread first(crash);
    std::thread second(crash);
    first.join();
    second.join();
  }
  return 0;
}
