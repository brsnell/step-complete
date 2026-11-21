# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

STEP Complete: a fast, lightweight, cross-platform viewer for STEP CAD files (AP203 / AP214 /
AP242) with exact measurement, AP242 PMI, section views and image export. C++17, Qt 6 Widgets,
OpenCASCADE (OCCT), CMake.

Product plan and milestones: `docs/PLAN.md`. Working order: `TODOS.md` (`docs/whats-next.md`
points there). Approved designs: `docs/designs/`. Dated plans: `docs/superpowers/plans/`.
Check those before assuming a feature is in scope.

## Commands

```bash
cmake --preset dev                 # configure (system Qt + OCCT, Debug, warnings as errors)
cmake --build --preset dev         # build
ctest --preset dev                 # all tests (render tests need a display, see Testing)
./build/dev/src/app/StepComplete   # run
tests/data/fetch.sh                # optional real-world STEP models for the RealModels tests
cmake --build --preset dev --target tidy   # naming check (clang-tidy; build first)
```

<!-- gstack:verify: cmake --build --preset dev && xvfb-run -a -s '-screen 0 1280x1024x24' ctest --preset dev -j4 -->
gstack's `/verify` runs the build plus the full ctest under Xvfb (declared by the hidden
`gstack:verify:` comment above). In a headless container set `LIBGL_ALWAYS_SOFTWARE=1`.
Release builds use the `vcpkg` presets (OCCT 8, Qt 6.8); see `README.md`.

## Architecture

Three layers, each a library under `src/`, plus the app's `main.cpp`:

- **`src/core`** (`sc::core`): OCCT only, no Qt. STEP import (`StepImporter`, `Document`,
  `CompressedStep`), the assembly index, units, the open-file cache (`DocumentCache`),
  measurement (`Measure`, `Measurement`) and the PMI model (`PmiModel`, `PmiText`).
- **`src/render`** (`sc::render`): the Qt ↔ OCCT bridge. `OcctViewport` (a `QOpenGLWidget`
  hosting a `V3d_View`), `SceneManager` (parts and selection), `MeasureTool` and its
  `WitnessPresentation`s, `PmiScene`, `SectionTool`.
- **`src/app`** (`sc::app`): Qt Widgets. `MainWindow`, the docks (assembly tree, properties,
  Measurements, PMI, Section, import messages), dialogs, and `ImportJob` (import off the GUI
  thread).

`core` never includes Qt; `render` never includes `app`.

## Naming conventions

The same conventions as Task Complete (`brsnell/RealTaskManagement`), adapted for C++: Qt and
OCCT name methods in `camelCase` and virtual overrides must keep the framework's names, so
methods stay `camelCase`; everything else C++ allows follows Task Complete. Decisions and
rationale: `docs/superpowers/plans/2026-11-21-naming-conventions.md`. clang-tidy
(`readability-identifier-naming`, configured in `.clang-tidy`) checks these rules: run the
`tidy` target locally; CI's `Naming (clang-tidy)` job fails on a violation.

| Element | Rule | Example |
|---|---|---|
| Types, enums, enum values | `PascalCase` | `MeasureTool`, `MeasureMode::PointToPoint` |
| Methods, free functions, Qt signals and slots | `camelCase` | `setWitnesses`, `displayChanged` |
| Public struct fields | `camelCase` | `Measurement::headline` |
| Private and protected members | `_camelCase` | `_viewport` |
| Private static members | `s_camelCase` | `s_fontState` |
| Constants (`constexpr`, namespace-scope `const`) | `PascalCase` | `constexpr double SnapPixels` |
| `const` locals | `camelCase` | `const int style = ...` |
| Parameters and locals | `camelCase`, spelled out | `const PendingPick& pendingPick`, not `p` |
| Macros and CMake options | `UPPER_CASE` | `SC_WARNINGS_AS_ERRORS` |
| Namespaces | lower case | `sc::core` |

No Hungarian prefixes (`m_`, `k`, type tags). Never a leading underscore at namespace scope or
before a capital letter (reserved in C++); `_camelCase` is only for class members.

Short names that stay: loop counters (`i`, `j`), `it`, `ok`, `id`, `ex`, `ctx`, one-line lambda
parameters, short whole words (`to`, as in `from`/`to`), and standard math and C++ shorthand:
`x`/`y`/`z` for coordinates and `u`/`v` for surface parameters, including their numbered, delta and
per-point forms (`x0`/`y1`, `dx`/`dy`, `du`/`dv`, `ax`/`ay` for point `a`), `a`/`b` for two things
being compared, `p1`/`p2` and `v1`/`v2` for pairs of points or vectors, `mm` for a value in
millimetres, `ec` for a `std::error_code`, `in`/`os`/`is` for streams, and `Pi`.

- **Never rename to fit the standard:** strings saved to disk or read by tools (`QSettings`
  keys, dock and widget `objectName`s (tests look widgets up by them), CSV headers,
  command-line flags); Qt and OCCT override names (`paintGL`, `event`, `Compute`); Qt Test's
  `*_data` methods; and test names (GoogleTest `TEST(Suite, Name)`, Qt Test slots and ctest
  names: CI and docs cite them).

## Testing

All tests live under `tests/`, run by ctest (`tests/CMakeLists.txt`):

- `core.*`: GoogleTest (`sc_core_tests`), OCCT only. The `RealModels`, `NistPmi` and
  `DocumentCache` model tests skip unless `SC_TEST_DATA_DIR` points at `tests/data/fetch.sh`'s
  downloads.
- `app.pmi_panel`, `app.preferences`: Qt Test on the `offscreen` platform (no display):
  `sc_app_tests` runs the PMI panel, Section panel, About dialog and Measurements panel
  classes (shared `main` in `tests/app/app_test_main.cpp`).
- Label `render` (need an OpenGL display, e.g. `xvfb-run` with Mesa): `render.viewport_smoke`
  (viewport, scene, measure tool, PMI scene, section tool), `render.pmi_snapshots` (NIST PMI
  images) and `app.main_window` (`MainWindow` wiring).

CI (`.github/workflows/ci.yml`) runs `ctest -LE render` on Linux, Windows and macOS, and the
`render` label under Xvfb + Mesa llvmpipe on Linux only. A clang-format check runs on every
push; format with `clang-format -i` (`.clang-format`, 110 columns).

## Releasing

The version lives in `VERSION` (`MAJOR.MINOR.PATCH`); `CMakeLists.txt` reads it, and
`vcpkg.json`'s `version-semver` must match. Record changes under `## [Unreleased]` in
`CHANGELOG.md`; at release, retitle that section `## [<version>] - <date>`, bump `VERSION` and
`vcpkg.json`, and push the tag `v<VERSION>`. `.github/workflows/release.yml` refuses a tag that
does not match `VERSION`, then builds the packages and publishes a GitHub Release (macOS DMGs
are built but not published yet; see `TODOS.md`). Merging to `main` deploys nothing.

## gstack (recommended)

This project uses [gstack](https://github.com/garrytan/gstack) for AI-assisted workflows.
Install it for the best experience:

```bash
git clone --depth 1 https://github.com/garrytan/gstack.git ~/.claude/skills/gstack
cd ~/.claude/skills/gstack && ./setup --team
```

Skills like /qa, /ship, /review and /browse become available after install.
Use /browse for all web browsing (Aside first, the bundled gstack browser as fallback). Use ~/.claude/skills/gstack/... for gstack file paths.

In Claude Code cloud sessions, `.claude/hooks/session-start.sh` installs gstack automatically at session start.
