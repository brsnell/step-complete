# Adopt Task Complete's naming conventions (adapted for C++)

Status: approved 2026-11-21. Landing as one PR per module.

## Goal

StepComplete follows the same naming conventions as Task Complete (`brsnell/RealTaskManagement`),
adapted where C++, Qt and OpenCASCADE differ from C# and JavaScript. The rules are written in
`CLAUDE.md` ("Naming conventions") and checked by clang-tidy.

## Decisions

- **Adapted, not literal.** Task Complete's C# uses `PascalCase` methods. Qt and OCCT use
  `camelCase` methods, and virtual overrides must keep the framework's names, so methods stay
  `camelCase`. Everything C++ allows follows Task Complete: `_camelCase` private and protected
  members, `s_camelCase` private statics, `PascalCase` constants, spelled-out parameters and
  locals, no Hungarian prefixes (`m_`, `k`).
- **Public struct fields stay `camelCase`**, so data and method calls read alike.
- **`const` locals stay `camelCase`.** Only `constexpr` and namespace-scope constants become
  `PascalCase`, the closest match to a C# `const`.
- **No `Async` suffix rule.** StepComplete has no `Task`-returning methods to mark.
- **Enforcement:** a clang-tidy job in CI plus a local `tidy` command, not inside every build
  (clang-tidy re-parses the OCCT headers and would slow every compile 2-3x).
- **Project docs adopted from Task Complete:** conventions in `CLAUDE.md`, `CHANGELOG.md` plus a
  `VERSION` file, `docs/designs/` and `docs/superpowers/plans/`, and `docs/whats-next.md`.
  Not adopted: per-directory `CLAUDE.md` files, `AGENTS.md` skill routing.

## Measured scope

A draft of the rules (clang-tidy 18, `readability-identifier-naming`) checks the whole repo in
54 seconds and finds 381 declarations to rename: 275 private members, 97 constants, 3 test-fixture
members, and 6 false positives (OCCT class names, Qt Test `*_data` methods), which the config
excludes. By folder: `src/app` 144, `src/render` 131, `src/core` 67, tests 39. Short names
(`p`, `v`, `m`, `s`) are not detected by that check; `readability-identifier-length` lists them
for a manual pass.

## PRs

1. **Rules and project docs.** No code changes. `CLAUDE.md` sections, `VERSION` (read by
   `CMakeLists.txt` and the release tag check), `CHANGELOG.md`, `docs/designs/`,
   `docs/superpowers/plans/`, `docs/whats-next.md`.
2. **Checker, report-only.** `.clang-tidy`, a local `tidy` command, a CI job that reports
   without failing.
3. **Rename `src/core`.** 4. **Rename `src/render`.** 5. **Rename `src/app`.**
6. **Rename tests and tools**, and make the CI job fail on a violation.

Each rename PR: `run-clang-tidy -fix` scoped to the module (it renames each declaration and all
of its uses, including uses in later modules); spell out short names by hand; fix any
`PascalCase` constant that collides with a type or enum value; `clang-format`; build with
warnings as errors; full `ctest` under Xvfb; review the diff as rename-only (no string literal
changes, no logic changes).
