#include "app/CrashHandler.h"

#include "core/OcctInfo.h"
#include "core/Version.h"

#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QSysInfo>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#if defined(_WIN32)
#include <windows.h>
// windows.h first: dbghelp.h needs its types.
#include <dbghelp.h>
#else
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#endif

namespace sc::app {

namespace {

// --- State prepared while the app runs (read by the handlers) -----------------------

constexpr size_t TextSize = 16384;
constexpr size_t MessageSize = 2048;
constexpr size_t PathSize = 2048;
constexpr int MaxFrames = 64;
constexpr size_t ContextBuffers = 3;
// How long a second crashing thread waits for the first one's report before ending the process
// itself, in tenths of a second.
constexpr int SecondCrashWaitTenths = 300;

// A ring of buffers: the setters fill the next one and then publish it, so a handler never reads
// a half-written text. Once a handler has started, the setters stop (nothing after the crash
// belongs in the report), so the buffer a handler took is never rewritten under it.
std::array<std::array<char, TextSize>, ContextBuffers> s_contextText{};
std::atomic<size_t> s_contextIndex{0};
std::array<char, MessageSize> s_message{};
std::atomic<bool> s_hasMessage{false};
std::time_t s_startTime = 0;
std::atomic<bool> s_handling{false};
bool s_installed = false;
bool s_installResult = false;

std::mutex s_contextMutex; // the setters only
QString s_environment;
std::map<CrashContext, QString> s_context;

#if defined(_WIN32)
std::array<wchar_t, PathSize> s_folder{};
using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                          PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
                                          PMINIDUMP_CALLBACK_INFORMATION);
MiniDumpWriteDumpFn s_writeDump = nullptr;
LPTOP_LEVEL_EXCEPTION_FILTER s_previousFilter = nullptr;
std::atomic<DWORD> s_handlingThread{0};
#else
std::array<char, PathSize> s_folder{};
std::atomic<std::uintptr_t> s_handlingThread{0};
#endif

QtMessageHandler s_previousQtHandler = nullptr;
std::terminate_handler s_previousTerminate = nullptr;

const char* contextLabel(CrashContext key) {
  switch (key) {
  case CrashContext::Graphics:
    return "Graphics";
  case CrashContext::Model:
    return "Model";
  case CrashContext::Activity:
    return "Activity";
  }
  return "";
}

void copyText(char* target, size_t size, const QByteArray& text) {
  const size_t count = std::min(size - 1, static_cast<size_t>(text.size()));
  std::memcpy(target, text.constData(), count);
  target[count] = '\0';
}

// Appends the C string 'source' to 'target' (of 'size' characters, 'used' so far), stopping one
// short of the end for the terminator. Returns the new length.
template <typename Char, typename Source>
size_t appendText(Char* target, size_t size, size_t used, const Source* source) {
  for (; *source != 0 && used + 1 < size; ++source) {
    target[used++] = static_cast<Char>(*source);
  }
  target[used] = 0;
  return used;
}

// Like the context: once a handler has started, another thread's message must not change the
// one it is writing.
void recordMessage(const QByteArray& message) {
  if (s_handling.load()) {
    return;
  }
  copyText(s_message.data(), MessageSize, message);
  s_hasMessage.store(true);
}

void publishContext() {
  if (s_handling.load()) {
    return;
  }
  QString text = s_environment;
  if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n'))) {
    text += QLatin1Char('\n');
  }
  for (const auto& [key, value] : s_context) {
    text += QStringLiteral("%1: %2\n").arg(QLatin1String(contextLabel(key)), value);
  }
  const size_t next = (s_contextIndex.load() + 1) % ContextBuffers;
  copyText(s_contextText[next].data(), TextSize, text.toUtf8());
  s_contextIndex.store(next);
}

// --- Formatting without the heap ------------------------------------------------------

// Writes into a fixed buffer; flushes to the file when full.
class ReportWriter {
public:
#if defined(_WIN32)
  explicit ReportWriter(HANDLE file) : _file(file) {}
#else
  explicit ReportWriter(int file) : _file(file) {}
#endif
  ~ReportWriter() { flush(); }

  ReportWriter(const ReportWriter&) = delete;
  ReportWriter& operator=(const ReportWriter&) = delete;

  void text(const char* text) {
    for (; text != nullptr && *text != '\0'; ++text) {
      put(*text);
    }
  }
  void number(std::uint64_t value, int base = 10, int minDigits = 1) {
    std::array<char, 24> digits{};
    int count = 0;
    do {
      const auto digit = static_cast<int>(value % static_cast<std::uint64_t>(base));
      digits[static_cast<size_t>(count++)] = static_cast<char>(digit < 10 ? '0' + digit : 'a' + digit - 10);
      value /= static_cast<std::uint64_t>(base);
    } while (value != 0 && count < 24);
    while (count < minDigits && count < 24) {
      digits[static_cast<size_t>(count++)] = '0';
    }
    while (count > 0) {
      put(digits[static_cast<size_t>(--count)]);
    }
  }
  void hex(std::uint64_t value) {
    text("0x");
    number(value, 16);
  }
  void flush() {
    if (_used == 0) {
      return;
    }
#if defined(_WIN32)
    DWORD written = 0;
    WriteFile(_file, _buffer.data(), static_cast<DWORD>(_used), &written, nullptr);
#else
    size_t done = 0;
    while (done < _used) {
      const ssize_t written = ::write(_file, _buffer.data() + done, _used - done);
      if (written <= 0) {
        break;
      }
      done += static_cast<size_t>(written);
    }
#endif
    _used = 0;
  }

private:
  void put(char character) {
    if (_used == _buffer.size()) {
      flush();
    }
    _buffer[_used++] = character;
  }

#if defined(_WIN32)
  HANDLE _file;
#else
  int _file;
#endif
  std::array<char, 1024> _buffer{};
  size_t _used = 0;
};

struct UtcTime {
  int year, month, day, hour, minute, second;
};

// Civil date from days since 1970-01-01 (Howard Hinnant's algorithm), so the POSIX handler needs
// no gmtime.
UtcTime utcFrom(std::time_t seconds) {
  const std::int64_t total = static_cast<std::int64_t>(seconds);
  std::int64_t days = total / 86400;
  std::int64_t rest = total % 86400;
  if (rest < 0) {
    rest += 86400;
    --days;
  }
  days += 719468;
  const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const auto dayOfEra = static_cast<unsigned>(days - era * 146097);
  const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const unsigned monthIndex = (5 * dayOfYear + 2) / 153;
  const unsigned day = dayOfYear - (153 * monthIndex + 2) / 5 + 1;
  const unsigned month = monthIndex < 10 ? monthIndex + 3 : monthIndex - 9;
  const std::int64_t year = static_cast<std::int64_t>(yearOfEra) + era * 400 + (month <= 2 ? 1 : 0);
  return {static_cast<int>(year),
          static_cast<int>(month),
          static_cast<int>(day),
          static_cast<int>(rest / 3600),
          static_cast<int>(rest % 3600 / 60),
          static_cast<int>(rest % 60)};
}

// "crash-20261125-142233" into 'name'.
void reportName(char* name, size_t size, std::time_t now) {
  const UtcTime time = utcFrom(now);
  const int fields[] = {time.year, time.month, time.day, -1, time.hour, time.minute, time.second};
  const int widths[] = {4, 2, 2, 0, 2, 2, 2};
  size_t used = appendText(name, size, 0, CrashReportPrefix);
  for (size_t i = 0; i < 7; ++i) {
    if (fields[i] < 0) {
      if (used + 1 < size) {
        name[used++] = '-';
      }
      continue;
    }
    int value = fields[i];
    std::array<char, 8> digits{};
    for (int position = widths[i] - 1; position >= 0; --position) {
      digits[static_cast<size_t>(position)] = static_cast<char>('0' + value % 10);
      value /= 10;
    }
    for (int position = 0; position < widths[i] && used + 1 < size; ++position) {
      name[used++] = digits[static_cast<size_t>(position)];
    }
  }
  name[used] = '\0';
}

void writeHeader(ReportWriter& out, std::time_t now) {
  out.text(core::AppName);
  out.text(" crash report\n\n");
  const UtcTime time = utcFrom(now);
  out.text("Time (UTC): ");
  out.number(static_cast<std::uint64_t>(time.year), 10, 4);
  out.text("-");
  out.number(static_cast<std::uint64_t>(time.month), 10, 2);
  out.text("-");
  out.number(static_cast<std::uint64_t>(time.day), 10, 2);
  out.text(" ");
  out.number(static_cast<std::uint64_t>(time.hour), 10, 2);
  out.text(":");
  out.number(static_cast<std::uint64_t>(time.minute), 10, 2);
  out.text(":");
  out.number(static_cast<std::uint64_t>(time.second), 10, 2);
  out.text("\nUptime: ");
  out.number(static_cast<std::uint64_t>(now >= s_startTime ? now - s_startTime : 0));
  out.text(" s\n");
}

void writeEnvironment(ReportWriter& out) {
  if (s_hasMessage.load()) {
    out.text("Message: ");
    out.text(s_message.data());
    out.text("\n");
  }
  out.text("\n");
  out.text(s_contextText[s_contextIndex.load()].data());
}

// --- Uncaught exceptions and Qt fatal messages -----------------------------------------

// Writes the report for a Qt fatal message where Qt does not end in abort() (Windows).
void reportFatalMessage();

void onTerminate() {
  QByteArray message = "Uncaught exception";
  if (const std::exception_ptr current = std::current_exception()) {
    try {
      std::rethrow_exception(current);
    } catch (const std::exception& ex) { // OCCT 8's Standard_Failure is one too
      message += QByteArray(": ") + ex.what();
#if OCC_VERSION_HEX < 0x080000
    } catch (const Standard_Failure& ex) { // a separate hierarchy before OCCT 8
      message += QByteArray(" (Open CASCADE ") + ex.DynamicType()->Name() + "): " + ex.GetMessageString();
#endif
    } catch (...) {
      message += " of an unknown type";
    }
  } else if (s_hasMessage.load()) {
    std::abort(); // keep what onUnhandledException recorded (Windows)
  } else {
    message = "std::terminate called without an exception";
  }
  recordMessage(message);
  std::abort();
}

void onQtMessage(QtMsgType type, const QMessageLogContext& context, const QString& text) {
  if (type == QtFatalMsg) {
    recordMessage((QStringLiteral("Qt fatal message: ") + text).toUtf8());
    reportFatalMessage();
  }
  if (s_previousQtHandler != nullptr) {
    s_previousQtHandler(type, context, text);
    return;
  }
  // No handler to hand on to (Qt 6 always returns its default one, so this is a safeguard):
  // print, and end on a fatal message as Qt would. Never qt_message_output(), which would call
  // this handler again.
  std::fprintf(stderr, "%s\n", text.toLocal8Bit().constData());
  if (type == QtFatalMsg) {
    std::abort();
  }
}

#if defined(_WIN32)

// --- Windows ------------------------------------------------------------------------
//
// The report is written by a reporter thread started at install: the crashed thread may have
// no stack left (a stack overflow) and MiniDumpWriteDump is meant to run on another thread.
// The crashed thread only records what happened, wakes the reporter and waits for it.

const char* exceptionName(DWORD code) {
  switch (code) {
  case EXCEPTION_ACCESS_VIOLATION:
    return "access violation";
  case EXCEPTION_STACK_OVERFLOW:
    return "stack overflow";
  case EXCEPTION_INT_DIVIDE_BY_ZERO:
    return "integer division by zero";
  case EXCEPTION_ILLEGAL_INSTRUCTION:
    return "illegal instruction";
  case EXCEPTION_IN_PAGE_ERROR:
    return "in-page error";
  case EXCEPTION_PRIV_INSTRUCTION:
    return "privileged instruction";
  case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    return "array bounds exceeded";
  case 0xE06D7363:
    return "C++ exception";
  case 0xC0000409:
    return "stack buffer overrun or fail-fast";
  default:
    return "exception";
  }
}

// What the crashed thread hands the reporter.
struct CrashRequest {
  const char* cause = nullptr;
  DWORD code = 0;
  const void* address = nullptr;
  EXCEPTION_POINTERS* pointers = nullptr;
  DWORD threadId = 0;
  ULONG_PTR stackLow = 0;
  ULONG_PTR stackHigh = 0;
  CONTEXT context{};
};
CrashRequest s_request;
HANDLE s_requestEvent = nullptr;
HANDLE s_doneEvent = nullptr;
std::atomic<DWORD> s_reporterThread{0}; // its own faults end the report at once

void writeFrame(ReportWriter& out, DWORD64 address) {
  out.text("  ");
  HMODULE module = nullptr;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(address), &module) &&
      module != nullptr) {
    std::array<wchar_t, MAX_PATH> path{};
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    const wchar_t* name = path.data();
    for (DWORD i = 0; i < length; ++i) {
      if (path[i] == L'\\' || path[i] == L'/') {
        name = path.data() + i + 1;
      }
    }
    // Module file names are ASCII (StepComplete.exe, Qt6Core.dll, TKernel.dll); anything else is
    // shown as '?' rather than converted, which needs no code page API in a crashed process.
    std::array<char, MAX_PATH> ascii{};
    size_t used = 0;
    for (const wchar_t* wide = name; *wide != L'\0' && used + 1 < ascii.size(); ++wide) {
      ascii[used++] = *wide < 0x80 ? static_cast<char>(*wide) : '?';
    }
    ascii[used] = '\0';
    out.text(ascii.data());
    out.text("+");
    out.hex(address - reinterpret_cast<DWORD64>(module));
  } else {
    out.hex(address);
  }
  out.text("\n");
}

#if defined(_M_X64)
// Unwinds from 'frame' into 'frames' and returns how many it found. The walk stays inside the
// crashed thread's stack (low..high) and is guarded with SEH, so a damaged stack ends the walk
// rather than the process (no objects with destructors here: __try does not allow them).
int unwindStack(CONTEXT frame, ULONG_PTR low, ULONG_PTR high, DWORD64* frames, int capacity) {
  int count = 0;
  __try {
    while (count < capacity && frame.Rip != 0 && frame.Rsp >= low && frame.Rsp < high) {
      frames[count++] = frame.Rip;
      DWORD64 imageBase = 0;
      PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(frame.Rip, &imageBase, nullptr);
      if (function == nullptr) {
        // A leaf function: the return address is on top of the stack.
        frame.Rip = *reinterpret_cast<const DWORD64*>(frame.Rsp);
        frame.Rsp += 8;
      } else {
        PVOID handlerData = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, frame.Rip, function, &frame, &handlerData,
                         &establisher, nullptr);
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    // keep the frames found so far
  }
  return count;
}
#endif

void writeStack(ReportWriter& out, const CrashRequest& request) {
  out.text("\nStack (module+offset):\n");
  std::array<DWORD64, MaxFrames> frames{};
#if defined(_M_X64)
  const int count =
      unwindStack(request.context, request.stackLow, request.stackHigh, frames.data(), MaxFrames);
#else
  (void)request;
  const int count = 0; // only x64 Windows is built
#endif
  for (int i = 0; i < count; ++i) {
    writeFrame(out, frames[static_cast<size_t>(i)]);
    out.flush(); // each frame reaches the file even if a later one faults
  }
}

HANDLE createReportFile(const char* name, const wchar_t* extension) {
  std::array<wchar_t, PathSize + 64> path{};
  size_t used = appendText(path.data(), path.size(), 0, s_folder.data());
  used = appendText(path.data(), path.size(), used, L"\\");
  used = appendText(path.data(), path.size(), used, name);
  appendText(path.data(), path.size(), used, extension);
  return CreateFileW(path.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

// On the reporter thread: the minidump first, then the text report, which says whether the
// minidump is there.
void writeReportFiles(const CrashRequest& request) {
  const std::time_t now = std::time(nullptr);
  std::array<char, 64> name{};
  reportName(name.data(), name.size(), now);

  const char* dumpNote = "\nNo minidump: dbghelp.dll could not be loaded.\n";
  if (s_writeDump != nullptr) {
    const HANDLE dump = createReportFile(name.data(), L".dmp");
    if (dump == INVALID_HANDLE_VALUE) {
      dumpNote = "\nNo minidump: the file could not be created.\n";
    } else {
      MINIDUMP_EXCEPTION_INFORMATION exception{request.threadId, request.pointers, FALSE};
      const auto type =
          static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
      const BOOL written = s_writeDump(GetCurrentProcess(), GetCurrentProcessId(), dump, type,
                                       request.pointers != nullptr ? &exception : nullptr, nullptr, nullptr);
      CloseHandle(dump);
      dumpNote = written ? "\nA minidump with the same name (.dmp) is in the same folder.\n"
                         : "\nNo minidump: writing it failed.\n";
    }
  }

  const HANDLE file = createReportFile(name.data(), L".txt");
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  {
    ReportWriter out(file);
    writeHeader(out, now);
    out.text("Cause: ");
    out.text(request.cause);
    if (request.code != 0) {
      out.text(" (");
      out.hex(request.code);
      out.text(")");
    }
    if (request.address != nullptr) {
      out.text(" at ");
      writeFrame(out, reinterpret_cast<DWORD64>(request.address));
    } else {
      out.text("\n");
    }
    writeEnvironment(out);
    out.flush(); // keep what is written if walking a damaged stack faults
    writeStack(out, request);
    out.text(dumpNote);
  }
  CloseHandle(file);
}

DWORD WINAPI reporterMain(void*) {
  if (WaitForSingleObject(s_requestEvent, INFINITE) == WAIT_OBJECT_0) {
    writeReportFiles(s_request);
    SetEvent(s_doneEvent);
  }
  return 0;
}

// On the crashed thread. A second thread that crashes meanwhile waits for the first one's report
// (whose end ends the process); the same thread failing again inside its own report, or the
// reporter thread failing while it writes, returns at once.
void writeReport(const char* cause, DWORD code, const void* address, EXCEPTION_POINTERS* pointers) {
  const DWORD self = GetCurrentThreadId();
  if (s_handling.exchange(true)) {
    if (s_handlingThread.load() != self && s_reporterThread.load() != self) {
      Sleep(SecondCrashWaitTenths * 100);
    }
    return;
  }
  s_handlingThread.store(self);
  s_request.cause = cause;
  s_request.code = code;
  s_request.address = address;
  s_request.pointers = pointers;
  s_request.threadId = self;
  GetCurrentThreadStackLimits(&s_request.stackLow, &s_request.stackHigh);
  if (pointers != nullptr && pointers->ContextRecord != nullptr) {
    s_request.context = *pointers->ContextRecord;
  } else {
    RtlCaptureContext(&s_request.context);
  }
  if (s_requestEvent != nullptr && s_doneEvent != nullptr) {
    SetEvent(s_requestEvent);
    WaitForSingleObject(s_doneEvent, SecondCrashWaitTenths * 100);
  } else {
    writeReportFiles(s_request); // no reporter thread: write it here
  }
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* pointers) {
  const DWORD code = pointers->ExceptionRecord->ExceptionCode;
  // An uncaught C++ exception: the runtime's own filter calls std::terminate, whose handler
  // records the exception's text and aborts into onAbort. Should the runtime ever reach
  // std::terminate without the exception, the report still says what kind it was.
  if (code == 0xE06D7363 && s_previousFilter != nullptr) {
    if (!s_hasMessage.load() && !s_handling.load()) {
      copyText(s_message.data(), MessageSize, "Uncaught C++ exception");
      s_hasMessage.store(true);
    }
    return s_previousFilter(pointers);
  }
  writeReport(exceptionName(code), code, pointers->ExceptionRecord->ExceptionAddress, pointers);
  return EXCEPTION_CONTINUE_SEARCH; // Windows Error Reporting still sees the crash
}

// Qt ends a fatal message with TerminateProcess on Windows, which no handler sees.
void reportFatalMessage() {
  writeReport("Qt fatal message", 0, nullptr, nullptr);
}

void onAbort(int) {
  writeReport("abort()", 0, nullptr, nullptr);
  // Leave without the CRT's abort message box; the process ends with the usual abort code.
  TerminateProcess(GetCurrentProcess(), 3);
}

void preparePlatformThread() {
  // Stack the filter can still use after a stack overflow (it hands the work to the reporter).
  ULONG guarantee = 32 * 1024;
  SetThreadStackGuarantee(&guarantee);
  std::set_terminate(onTerminate); // per thread in MSVC's runtime
}

bool installPlatform(const QString& folder) {
  const std::wstring native = QDir::toNativeSeparators(folder).toStdWString();
  if (native.size() + 1 > s_folder.size()) {
    return false;
  }
  std::copy(native.begin(), native.end(), s_folder.begin());
  s_folder[native.size()] = L'\0';
  // Loaded now: a crashed process should not load libraries.
  if (const HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll")) {
    s_writeDump = reinterpret_cast<MiniDumpWriteDumpFn>(
        reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")));
  }
  s_requestEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  s_doneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (s_requestEvent != nullptr && s_doneEvent != nullptr) {
    DWORD reporterId = 0;
    if (const HANDLE reporter = CreateThread(nullptr, 256 * 1024, reporterMain, nullptr, 0, &reporterId)) {
      s_reporterThread.store(reporterId);
      CloseHandle(reporter);
    } else {
      CloseHandle(s_requestEvent);
      CloseHandle(s_doneEvent);
      s_requestEvent = nullptr;
      s_doneEvent = nullptr;
    }
  }
  s_previousFilter = SetUnhandledExceptionFilter(onUnhandledException);
  std::signal(SIGABRT, onAbort);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  preparePlatformThread();
  return true;
}

#else

// --- Linux and macOS -------------------------------------------------------------------

const char* signalName(int signal) {
  switch (signal) {
  case SIGSEGV:
    return "SIGSEGV (segmentation fault)";
  case SIGBUS:
    return "SIGBUS (bus error)";
  case SIGFPE:
    return "SIGFPE (arithmetic error)";
  case SIGILL:
    return "SIGILL (illegal instruction)";
  case SIGABRT:
    return "SIGABRT (abort)";
  default:
    return "signal";
  }
}

// Qt ends a fatal message with abort(): onSignal writes the report.
void reportFatalMessage() {}

constexpr int HandledSignals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};

std::uintptr_t currentThreadId() {
  const pthread_t self = pthread_self();
  std::uintptr_t id = 0;
  std::memcpy(&id, &self, std::min(sizeof(id), sizeof(self)));
  return id;
}

// Each prepared thread's alternate signal stack, so a stack overflow there leaves the handler a
// stack to run on. Released when the thread ends.
struct AlternateStack {
  std::unique_ptr<char[]> memory;
  ~AlternateStack() {
    if (memory) {
      stack_t disable{};
      disable.ss_flags = SS_DISABLE;
      sigaltstack(&disable, nullptr);
    }
  }
};
thread_local AlternateStack s_threadStack;

void preparePlatformThread() {
  if (s_threadStack.memory) {
    return;
  }
  const size_t size = std::max<size_t>(64 * 1024, static_cast<size_t>(SIGSTKSZ));
  s_threadStack.memory.reset(new char[size]);
  stack_t stack{};
  stack.ss_sp = s_threadStack.memory.get();
  stack.ss_size = size;
  if (sigaltstack(&stack, nullptr) != 0) {
    s_threadStack.memory.reset();
  }
}

void onSignal(int signal, siginfo_t* info, void*) {
  if (s_handling.exchange(true)) {
    // Another thread is writing the report: let it finish (its re-raise ends the process). The
    // same thread failing inside its own report goes straight on.
    if (s_handlingThread.load() != currentThreadId()) {
      for (int i = 0; i < SecondCrashWaitTenths; ++i) {
        timespec tenth{0, 100 * 1000 * 1000};
        nanosleep(&tenth, nullptr);
      }
    }
  } else {
    s_handlingThread.store(currentThreadId());
    const std::time_t now = std::time(nullptr);
    std::array<char, 64> name{};
    reportName(name.data(), name.size(), now);
    std::array<char, PathSize + 64> path{};
    size_t used = appendText(path.data(), path.size(), 0, s_folder.data());
    used = appendText(path.data(), path.size(), used, "/");
    used = appendText(path.data(), path.size(), used, name.data());
    appendText(path.data(), path.size(), used, ".txt");
    const int file = ::open(path.data(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file >= 0) {
      {
        ReportWriter out(file);
        writeHeader(out, now);
        out.text("Cause: ");
        out.text(signalName(signal));
        if (signal != SIGABRT && info != nullptr) {
          out.text(" at address ");
          out.hex(reinterpret_cast<std::uintptr_t>(info->si_addr));
        }
        out.text("\n");
        writeEnvironment(out);
        out.text("\nStack:\n");
      }
      std::array<void*, MaxFrames> frames{};
      const int count = backtrace(frames.data(), MaxFrames);
      backtrace_symbols_fd(frames.data(), count, file);
      ::close(file);
    }
  }
  // Back to the default action, so the OS still sees the crash (core dump, crash reporter).
  struct sigaction action {};
  action.sa_handler = SIG_DFL;
  sigemptyset(&action.sa_mask);
  sigaction(signal, &action, nullptr);
  raise(signal);
}

bool installPlatform(const QString& folder) {
  const QByteArray native = QFile::encodeName(folder);
  if (static_cast<size_t>(native.size()) + 1 > s_folder.size()) {
    return false;
  }
  copyText(s_folder.data(), s_folder.size(), native);
  // The first backtrace() loads libgcc's unwinder; do it now rather than in a crashed process.
  std::array<void*, 4> warmUp{};
  backtrace(warmUp.data(), static_cast<int>(warmUp.size()));

  preparePlatformThread(); // this thread's alternate stack

  struct sigaction action {};
  action.sa_sigaction = onSignal;
  action.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&action.sa_mask);
  for (const int signal : HandledSignals) {
    sigaction(signal, &action, nullptr);
  }
  return true;
}

#endif

} // namespace

bool installCrashHandler(const QString& folder) {
  if (s_installed) {
    return s_installResult;
  }
  s_installed = true;
  if (!QDir().mkpath(folder)) {
    return false;
  }
  s_startTime = std::time(nullptr);
  {
    const std::lock_guard<std::mutex> lock(s_contextMutex);
    publishContext();
  }
  if (!installPlatform(QDir(folder).absolutePath())) {
    return false;
  }
  s_previousTerminate = std::set_terminate(onTerminate);
  s_previousQtHandler = qInstallMessageHandler(onQtMessage);
  s_installResult = true;
  return true;
}

bool isCrashHandlerInstalled() {
  return s_installResult;
}

void prepareCrashHandlerThread() {
  if (s_installResult) {
    preparePlatformThread();
  }
}

void setCrashEnvironment(const QString& text) {
  const std::lock_guard<std::mutex> lock(s_contextMutex);
  s_environment = text;
  publishContext();
}

QString defaultCrashEnvironment() {
  QString text;
  text += QStringLiteral("Version: %1 %2\n")
              .arg(QString::fromUtf8(core::AppName), QString::fromUtf8(core::AppVersion));
  text += QStringLiteral("Qt: %1 (built with %2)\n")
              .arg(QString::fromLatin1(qVersion()), QStringLiteral(QT_VERSION_STR));
  text += QStringLiteral("Open CASCADE: %1\n").arg(QString::fromStdString(core::occtVersion()));
  text += QStringLiteral("OS: %1 (%2 %3, %4)\n")
              .arg(QSysInfo::prettyProductName(), QSysInfo::kernelType(), QSysInfo::kernelVersion(),
                   QSysInfo::currentCpuArchitecture());
  return text;
}

void setCrashContext(CrashContext key, const QString& value) {
  const std::lock_guard<std::mutex> lock(s_contextMutex);
  s_context[key] = value;
  publishContext();
}

void setCrashMessage(const QString& message) {
  recordMessage(message.toUtf8());
}

QString crashContextText() {
  const std::lock_guard<std::mutex> lock(s_contextMutex);
  return QString::fromUtf8(s_contextText[s_contextIndex.load()].data());
}

} // namespace sc::app
