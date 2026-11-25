# Crash reporting

Status: APPROVED (2026-11-25)

## What

When STEP Complete crashes, it writes a **crash report** to its data folder before it exits: what
crashed (signal or exception, address), the call stack, the versions and graphics driver, and
what the app was doing. On Windows a **minidump** is written beside it.

The next time the app starts, it says so:

> STEP Complete closed unexpectedly last time.

The dialog shows the report (read-only, so the user can see everything it contains) with:

- **Report on GitHub...**: opens a new GitHub issue in the browser, titled and filled in with the
  report, for the user to review and submit (and to attach the minidump to);
- **Copy Report** and **Open Folder** (the crash folder, for the minidump);
- **Close**.

**Help > Crash Reports...** opens the crash folder at any time.

## Why

Field crashes on GPUs and drivers that CI never sees otherwise go unreported, or arrive as "it
crashed" without a stack (TODOS, Deferred: crash reporting, now that v1.0 is in users' hands).

## Decisions

1. **Nothing leaves the machine by itself.** No crash server and no third-party service (Sentry,
   Backtrace, BugSplat): they cost money, need a privacy policy and consent, and send data the
   user never sees. The report is a local file the user reads in full before choosing to send it,
   through a GitHub issue they can edit first. This fits an open-source viewer with no backend.
2. **No crash library.** Crashpad and Breakpad need an out-of-process handler and an upload
   server to be worth their size; without a server they add little over the OS's own tools.
   The handler is about 400 lines of platform code:
   - **Windows:** an unhandled-exception filter hands the crash to a reporter thread started at
     install, which writes the minidump (`MiniDumpWriteDump`, in `dbghelp.dll`, part of Windows)
     and then the text report. The crashed thread may have no stack left (a stack overflow), and
     `MiniDumpWriteDump` is meant to run on another thread. The stack is unwound from the
     exception's context with `RtlLookupFunctionEntry` / `RtlVirtualUnwind` inside the crashed
     thread's stack limits, one line per frame as module + offset (`StepComplete.exe+0x1a2b3c`).
     The report says whether the minidump was written.
   - **Linux and macOS:** handlers for `SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL` and `SIGABRT` run on
     the thread's alternate stack, write the report with `backtrace` / `backtrace_symbols_fd` (module,
     symbol where the binary exports one, offset), then restore the default action and re-raise,
     so the OS still sees a crash (core dump, macOS crash reporter).
   - **Everywhere:** an uncaught C++ exception (`std::set_terminate`) and a Qt fatal message
     (`qFatal`, by chaining the message handler) record their text, then abort into the handlers
     above, so the report says which exception or message it was.
   - **Threads:** a crash on any thread is reported. Each thread the app starts (the background
     jobs) calls `prepareCrashHandlerThread()` first, which gives it what two cases need there: an
     alternate signal stack of its own (POSIX; one per thread, so a stack overflow there leaves
     the handler room) and, on Windows, a stack guarantee for the filter and the terminate
     handler (MSVC keeps it per thread). When two threads crash at once, the second waits for the
     first one's report, whose end ends the process.
3. **Signal safety.** A crashed process can't allocate or take locks. Everything the report
   needs that is known in advance (the folder, the versions, the graphics driver, what the app
   was doing) is formatted into fixed buffers while the app runs, published with an atomic
   index over a ring of three buffers, so a crash never reads a half-written buffer. The handlers then only format numbers into
   stack buffers and call `open`/`write` (`CreateFileW`/`WriteFile` on Windows).
4. **What the report contains, and what it does not.** App version, Qt and OCCT versions, OS and
   CPU architecture, the OpenGL driver summary, the uptime, what was running (importing, a
   measurement mode, the wall thickness view, ...), and the open model's **format, size and
   number of parts**. Never the file's name or path, part names, or any geometry: model names
   are often confidential. Stack lines on Linux and macOS name each library by its path, so the
   app's own install folder appears there (the user sees it before sending). The minidump holds each thread's stack memory and the loaded module list (the small
   `MiniDumpNormal` kind plus thread and module info, a few hundred KB), not the heap. A
   background job's stack can still hold fragments of the model (a few coordinates, a short
   name), so attaching the minidump stays the user's choice, and the issue text says what it holds.
5. **Symbols.** A module + offset line only becomes a function name with the build's debug
   information. Windows release builds now produce a PDB (`/Z7`, so parallel compiles share no
   compiler PDB, linked `/DEBUG /OPT:REF /OPT:ICF`, so the exe is no bigger) and each release publishes it as
   `StepComplete-<version>-windows-x64-symbols.zip`: a minidump opened in Visual Studio or WinDbg
   with it shows the full stack. Linux and macOS packages keep their exported symbols only;
   shipping their separate debug info is a follow-up (TODOS) if reports come in from them.
6. **Where reports go.** `<AppData>/crashes/crash-<UTC date-time>.txt` (and `.dmp`), the folder
   the per-model store's parent uses. The newest 10 reports are kept; older ones are removed at
   start. A report is "new" until the dialog has shown it once (a `last-seen` file holds the
   newest name shown), so the dialog never repeats and a crash loop shows one dialog per start
   at most.
7. **The issue link.** `https://github.com/brsnell/step-complete/issues/new?title=...&body=...`
   with the report in a code block, and on Windows a line asking to attach the minidump if the
   user can, saying what it holds. The report is cut by whole lines from the end until the encoded link is at most 8,000 bytes, which
   browsers and GitHub accept (the limit counts the encoded link: a stack line's slashes, spaces
   and brackets take three bytes each); the cut is marked.
8. **Turning it off.** `--no-crash-handler` on the command line skips installing the handlers
   (for debugging the app under a debugger or with sanitizers, which install their own).
9. **Code layout.**
   - `app/CrashHandler.{h,cpp}`: `installCrashHandler(folder)`, `setCrashContext(...)`; the
     platform code, no Qt in the handlers.
   - `app/CrashReports.{h,cpp}`: the folder's reports (new ones, pruning, last seen), the issue
     link, and `CrashReportDialog`.
   - `main.cpp` installs the handler and shows the dialog after the window appears;
     `MainWindow` keeps the context current; the Help menu item.
   - `CompilerSettings.cmake` (the PDB), `packaging/windows/build-package.ps1` and
     `release.yml` (the symbols zip).

## Out of scope

- Sending reports automatically, or a server to collect them.
- Hang detection (a frozen UI is not a crash).
- Separate debug info for the Linux and macOS packages (see Decision 5).
- Symbols for OCCT and Qt DLLs: their frames show as module + offset.
- Stack overflows on OCCT's own pool threads (`OSD_Parallel`, parallel meshing): the app cannot
  prepare those threads, so an overflow there ends without a report. Other crashes on them are
  reported.

## Tests

- `app.crash_handler` (each platform's CI): a helper program installs the handler into a temp
  folder and crashes seven ways (a null pointer write, `abort()`, an uncaught exception, `qFatal`,
  and on prepared worker threads a stack overflow, an uncaught exception, and two threads
  faulting at once); the test checks it ended abnormally and that the one report names the
  cause (and the exception's text), has the versions and the context, and has stack frames; on
  Windows that the minidump is there and not empty.
- `app.main_window`: the context says "importing" while a file loads and "viewing" after, gives
  the model's format and part count, and never its file name.
- `app.pmi_panel` (offscreen): `CrashReports` finds new reports, keeps 10, remembers the last
  seen; the issue link's title, body and encoding, and that a long Linux stack is cut by whole
  lines until the encoded link fits; `CrashReportDialog` shows the report and
  its buttons.
