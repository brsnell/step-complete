# Crash reporting: fixes from the code review

Status: planned 2026-11-25. Source: `/code-review` of crash reporting (PR #34, merged as
`7cf5b29`), 10 findings (6 important, 4 nit). Each finding below was checked against the merged
code before it was recorded; all 10 hold. Design: `docs/designs/crash-reporting.md`.

All fixes land on `fix/crash-reporting-review` as one PR, one commit per group below, each with
its test, before the next release (crash reporting is still under Unreleased, so no release has
shipped the problems).

## Findings

| # | Sev | Where | Problem |
|---|---|---|---|
| 1 | Important | `MainWindow.cpp` `openFile`, `showWallThickness` | The Activity line is refreshed only from `updateActions()`, which neither calls after starting its job: a crash while importing (the likeliest moment) reports "viewing". |
| 2 | Important | `CrashHandler.cpp` `installPlatform` | `sigaltstack` is per thread; only the main thread has one, so a stack overflow on a worker thread faults again in the handler and leaves no report. On Windows the handler runs on what is left of the overflowed stack (no `SetThreadStackGuarantee`). |
| 3 | Important | `CrashReports.cpp` `issueUrl` | The 6,000-character cap is counted before percent-encoding; a Linux backtrace encodes to a 10–15 KB URL that GitHub or the browser may refuse (414) or cut. |
| 4 | Important | `CrashHandler.cpp` `onSignal` | A second crashing thread finds `s_handling` set, skips the report but re-raises with the default action at once, killing the process while the first thread is still writing. |
| 5 | Important | `CrashHandler.cpp` `installCrashHandler` | MSVC keeps the `std::set_terminate` handler per thread: an uncaught exception on a worker thread reports only "Uncaught C++ exception" / "abort()", without its text. |
| 6 | Important | `CrashHandler.cpp` `publishContext` | With two context buffers, two quick setter calls can rewrite the buffer a crashing thread is reading (the GUI thread keeps running while a worker's handler formats), mixing old and new text. |
| 7 | Nit | `CrashHandler.cpp` `writeReport` | Windows reports always say a minidump is beside them, also when dbghelp did not load or the .dmp could not be created. |
| 8 | Nit | `CrashHandler.cpp` `onQtMessage` | The fallback for a null previous handler calls `qt_message_output`, which dispatches back to `onQtMessage`: endless recursion (latent: Qt 6 returns its default handler, never null). |
| 9 | Nit | `docs/designs/crash-reporting.md` decision 5 | Says `/Zi`; the build uses `/Z7` (on purpose, no shared compiler PDB). |
| 10 | Nit | `CrashHandler.cpp` (`put(char c)`, loops over `p`/`c`) | Single-letter names against CLAUDE.md's naming rule (clang-tidy checks only case, so CI did not catch it). |

## Plan

### A. Context that is right and never torn (findings 1, 6)

- `MainWindow`: call `updateCrashActivity()` right after each job's `start()` (import, export,
  geometry check, clash check, wall thickness, comparison) and in each `on...Finished` before the
  pointer is cleared, rather than relying on `updateActions()`. A test-only accessor
  `crashContextText()` in `CrashHandler.h` returns the published text.
- `publishContext`: three buffers in a ring instead of two, and setters stop publishing once a
  handler has started (`s_handling` set): nothing after the crash belongs in the report anyway.
  A writer then never touches the buffer a handler took, short of two publishes during one
  handler's read, which the `s_handling` check rules out.
- Tests: `app.main_window`, opening the fixture shows "importing" in the context while the job
  runs and "viewing" after; a unit test that publishing after `s_handling` is set leaves the text
  unchanged.

### B. Worker threads (findings 2, 5)

- New `prepareCrashHandlerThread()` in `CrashHandler.h`, called first thing in every job's thread
  function (the six `QThread::create` lambdas in `app/*Job.cpp`):
  - POSIX: allocates and installs that thread's alternate signal stack (freed at thread exit
    through a `thread_local` holder).
  - Windows: `SetThreadStackGuarantee(64 KB)` so the filter has room after an overflow, and
    `std::set_terminate(onTerminate)` because MSVC keeps it per thread.
  - The main thread gets the same in `installCrashHandler`.
- Limitation to record in the design: OCCT's own pool threads (`OSD_Parallel`, parallel meshing)
  cannot be hooked, so a stack overflow inside them still ends without a report; an ordinary
  fault there is reported.
- Tests: new helper modes in `sc_crash_helper`: `thread-overflow` (unbounded recursion on a
  prepared `std::thread`), `thread-exception` (throw on a prepared thread). The report must have
  the cause and, for the exception, its text on every platform.

### C. Two crashes at once (finding 4)

- `onSignal`: a thread that finds `s_handling` already set waits (`pause()` in a loop, with the
  signal unblocked for the first thread's re-raise) instead of re-raising, so the first handler
  finishes the report and then ends the process. Windows: the filter's second entrant waits on
  the same flag with `Sleep` for at most a few seconds.
- Test: helper mode `two-threads`, two prepared threads fault together; the one report is
  complete (has the `Stack` section and its last line).

### D. An issue link that always opens (finding 3)

- `CrashReports::issueUrl`: measure the percent-encoded query, and cut the report (at a line
  break) until the whole URL fits `MaxIssueUrl` = 8,000 bytes; keep the cut marker.
- Test: a 64-frame Linux-style backtrace gives a URL of at most 8,000 bytes that still holds the
  header lines and the cut marker.

### E. Smaller fixes (findings 7, 8, 9, 10)

- Write the minidump first, and add the "minidump beside it" line only when
  `MiniDumpWriteDump` succeeded; the text report says "no minidump (reason)" otherwise.
- `onQtMessage` without a previous handler: print to stderr and `std::abort()` on a fatal message.
- Design decision 5: `/Z7`, with the reason.
- Spell out the short names (`character`, `source`, `wide`).

### Done when

- The full ctest passes locally under Xvfb, and CI is green on Linux, Windows and both macOS jobs,
  with the new helper modes running on each.
- Naming and format checks are clean; the design doc's limitations and CHANGELOG (Unreleased)
  match the behaviour.
