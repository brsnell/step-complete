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
#include <cstdint>
#include <cstring>
#include <ctime>
#include <exception>
#include <map>
#include <mutex>
#include <string>

#if defined(_WIN32)
#include <windows.h>
// windows.h first: dbghelp.h needs its types.
#include <dbghelp.h>
#else
#include <csignal>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace sc::app {

namespace {

// --- State prepared while the app runs (read by the handlers) -----------------------

constexpr size_t TextSize = 16384;
constexpr size_t MessageSize = 2048;
constexpr size_t PathSize = 2048;
constexpr int MaxFrames = 64;

// Two buffers: the setters fill the one not in use and then publish it, so a handler never reads
// a half-written text.
std::array<std::array<char, TextSize>, 2> s_contextText{};
std::atomic<int> s_contextIndex{0};
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
#else
std::array<char, PathSize> s_folder{};
std::array<char, 65536> s_alternateStack{};
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

void publishContext() {
  QString text = s_environment;
  if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n'))) {
    text += QLatin1Char('\n');
  }
  for (const auto& [key, value] : s_context) {
    text += QStringLiteral("%1: %2\n").arg(QLatin1String(contextLabel(key)), value);
  }
  const int next = 1 - s_contextIndex.load();
  copyText(s_contextText[static_cast<size_t>(next)].data(), TextSize, text.toUtf8());
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
  void put(char c) {
    if (_used == _buffer.size()) {
      flush();
    }
    _buffer[_used++] = c;
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
  size_t used = 0;
  for (const char* p = CrashReportPrefix; *p != '\0' && used + 1 < size; ++p) {
    name[used++] = *p;
  }
  for (size_t i = 0; i < 7; ++i) {
    if (fields[i] < 0) {
      if (used + 1 < size) {
        name[used++] = '-';
      }
      continue;
    }
    int value = fields[i];
    std::array<char, 8> digits{};
    for (int d = widths[i] - 1; d >= 0; --d) {
      digits[static_cast<size_t>(d)] = static_cast<char>('0' + value % 10);
      value /= 10;
    }
    for (int d = 0; d < widths[i] && used + 1 < size; ++d) {
      name[used++] = digits[static_cast<size_t>(d)];
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
  out.text(s_contextText[static_cast<size_t>(s_contextIndex.load())].data());
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
    std::abort(); // already said (Windows: "Uncaught C++ exception", see onUnhandledException)
  } else {
    message = "std::terminate called without an exception";
  }
  copyText(s_message.data(), MessageSize, message);
  s_hasMessage.store(true);
  std::abort();
}

void onQtMessage(QtMsgType type, const QMessageLogContext& context, const QString& text) {
  if (type == QtFatalMsg) {
    copyText(s_message.data(), MessageSize, (QStringLiteral("Qt fatal message: ") + text).toUtf8());
    s_hasMessage.store(true);
    reportFatalMessage();
  }
  if (s_previousQtHandler != nullptr) {
    s_previousQtHandler(type, context, text);
  } else {
    // Qt's default: print, and abort on a fatal message.
    qt_message_output(type, context, text);
  }
}

#if defined(_WIN32)

// --- Windows ------------------------------------------------------------------------

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
    std::array<char, MAX_PATH * 3> utf8{};
    WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8.data(), static_cast<int>(utf8.size()), nullptr, nullptr);
    out.text(utf8.data());
    out.text("+");
    out.hex(address - reinterpret_cast<DWORD64>(module));
  } else {
    out.hex(address);
  }
  out.text("\n");
}

void writeStack(ReportWriter& out, const CONTEXT* context) {
  out.text("\nStack (module+offset):\n");
#if defined(_M_X64)
  CONTEXT frame = *context;
  for (int i = 0; i < MaxFrames && frame.Rip != 0; ++i) {
    writeFrame(out, frame.Rip);
    DWORD64 imageBase = 0;
    PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(frame.Rip, &imageBase, nullptr);
    if (function == nullptr) {
      // A leaf function: the return address is on top of the stack.
      if (frame.Rsp == 0) {
        break;
      }
      frame.Rip = *reinterpret_cast<const DWORD64*>(frame.Rsp);
      frame.Rsp += 8;
    } else {
      PVOID handlerData = nullptr;
      DWORD64 establisher = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, frame.Rip, function, &frame, &handlerData, &establisher,
                       nullptr);
    }
  }
#else
  (void)context;
  std::array<void*, MaxFrames> frames{};
  const USHORT count = CaptureStackBackTrace(0, MaxFrames, frames.data(), nullptr);
  for (USHORT i = 0; i < count; ++i) {
    writeFrame(out, reinterpret_cast<DWORD64>(frames[i]));
  }
#endif
}

HANDLE createReportFile(const char* name, const wchar_t* extension) {
  std::array<wchar_t, PathSize + 64> path{};
  size_t used = 0;
  for (const wchar_t* p = s_folder.data(); *p != L'\0' && used + 1 < path.size(); ++p) {
    path[used++] = *p;
  }
  if (used + 1 < path.size()) {
    path[used++] = L'\\';
  }
  for (const char* p = name; *p != '\0' && used + 1 < path.size(); ++p) {
    path[used++] = static_cast<wchar_t>(*p);
  }
  for (const wchar_t* p = extension; *p != L'\0' && used + 1 < path.size(); ++p) {
    path[used++] = *p;
  }
  path[used] = L'\0';
  return CreateFileW(path.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void writeReport(const char* cause, DWORD code, const void* address, EXCEPTION_POINTERS* pointers) {
  if (s_handling.exchange(true)) {
    return;
  }
  const std::time_t now = std::time(nullptr);
  std::array<char, 64> name{};
  reportName(name.data(), name.size(), now);

  const HANDLE file = createReportFile(name.data(), L".txt");
  if (file != INVALID_HANDLE_VALUE) {
    {
      ReportWriter out(file);
      writeHeader(out, now);
      out.text("Cause: ");
      out.text(cause);
      if (code != 0) {
        out.text(" (");
        out.hex(code);
        out.text(")");
      }
      if (address != nullptr) {
        out.text(" at ");
        writeFrame(out, reinterpret_cast<DWORD64>(address));
      } else {
        out.text("\n");
      }
      writeEnvironment(out);
      out.flush(); // keep what is written if walking a damaged stack faults
      CONTEXT here{};
      const CONTEXT* context = pointers != nullptr ? pointers->ContextRecord : nullptr;
      if (context == nullptr) {
        RtlCaptureContext(&here);
        context = &here;
      }
      writeStack(out, context);
      out.text("\nA minidump with the same name (.dmp) is in the same folder.\n");
    }
    CloseHandle(file);
  }

  if (s_writeDump != nullptr) {
    const HANDLE dump = createReportFile(name.data(), L".dmp");
    if (dump != INVALID_HANDLE_VALUE) {
      MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), pointers, FALSE};
      const auto type =
          static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
      s_writeDump(GetCurrentProcess(), GetCurrentProcessId(), dump, type,
                  pointers != nullptr ? &exception : nullptr, nullptr, nullptr);
      CloseHandle(dump);
    }
  }
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* pointers) {
  const DWORD code = pointers->ExceptionRecord->ExceptionCode;
  // An uncaught C++ exception: the runtime's own filter calls std::terminate, whose handler
  // records the exception's text and aborts into onAbort.
  // MSVC gives std::terminate no current exception there, so say what it was now.
  if (code == 0xE06D7363 && s_previousFilter != nullptr) {
    if (!s_hasMessage.load()) {
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
  s_previousFilter = SetUnhandledExceptionFilter(onUnhandledException);
  std::signal(SIGABRT, onAbort);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
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

void onSignal(int signal, siginfo_t* info, void*) {
  if (!s_handling.exchange(true)) {
    const std::time_t now = std::time(nullptr);
    std::array<char, 64> name{};
    reportName(name.data(), name.size(), now);
    std::array<char, PathSize + 64> path{};
    size_t used = 0;
    for (const char* p = s_folder.data(); *p != '\0' && used + 1 < path.size(); ++p) {
      path[used++] = *p;
    }
    path[used++] = '/';
    for (const char* p = name.data(); *p != '\0' && used + 1 < path.size(); ++p) {
      path[used++] = *p;
    }
    for (const char* p = ".txt"; *p != '\0' && used + 1 < path.size(); ++p) {
      path[used++] = *p;
    }
    path[used] = '\0';
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

  stack_t stack{};
  stack.ss_sp = s_alternateStack.data();
  stack.ss_size = s_alternateStack.size();
  sigaltstack(&stack, nullptr); // a stack overflow leaves no stack to run the handler on

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
  copyText(s_message.data(), MessageSize, message.toUtf8());
  s_hasMessage.store(true);
}

} // namespace sc::app
