#pragma once

#include <QString>

namespace sc::app {

// Crash reports (docs/designs/crash-reporting.md). When the process crashes (a fatal signal on
// Linux and macOS, an unhandled exception on Windows, abort(), an uncaught C++ exception or a Qt
// fatal message), a text report is written to the folder before the process ends: the cause,
// the call stack, the environment and the context below. On Windows a minidump is written
// beside it. Nothing is sent anywhere.
//
// The handlers cannot allocate or lock, so everything known in advance is formatted while the
// app runs: call the setters from the GUI thread whenever something changes.

// Installs the handlers (once per process; later calls only return the first result). Creates
// the folder. Returns false when the folder cannot be created.
bool installCrashHandler(const QString& folder);
bool isCrashHandlerInstalled();

// Call first thing on every thread the app starts (the background jobs), after installCrashHandler.
// A crash on any thread is reported either way; this makes two cases work there too: a stack
// overflow (POSIX: the thread's own alternate signal stack) and, on Windows, the text of an
// uncaught exception (MSVC keeps std::set_terminate per thread). Does nothing when the handler
// is not installed. OCCT's own pool threads cannot be prepared (design, out of scope).
void prepareCrashHandlerThread();

// Fixed facts for the report: versions, OS. Several lines.
void setCrashEnvironment(const QString& text);
// The default environment text: app, Qt and OCCT versions, OS, kernel and CPU architecture.
QString defaultCrashEnvironment();

// Context lines that change while the app runs. Never file names or paths (design, decision 4).
enum class CrashContext { Graphics, Model, Activity };
void setCrashContext(CrashContext key, const QString& value);

// Recorded just before a deliberate abort (uncaught exception, Qt fatal message) so the report
// says what it was.
void setCrashMessage(const QString& message);

// The environment and context text a report would contain now (for tests).
QString crashContextText();

// The report file name prefix and extensions (crash-<UTC yyyyMMdd-HHmmss>.txt / .dmp).
inline constexpr const char* CrashReportPrefix = "crash-";

} // namespace sc::app
