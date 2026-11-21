# Changelog

All notable changes to STEP Complete. The version lives in `VERSION`; a release is the
tag `v<VERSION>`, which builds the packages and publishes a GitHub Release.

## [Unreleased]

### Added
- Measurements: every successful result has a checkbox, and all checked results are drawn
  in the 3D view at once. The selected one is full orange, the others thinner and paler.
  New results start checked. A Show All / Hide All button sits next to Export CSV.
- Measurements: `Delete` removes the selected results (`Backspace` too on macOS), and the
  list has a right-click menu: Copy, Remove, Show in 3D View, Show All, Hide All, Clear All.

### Changed
- Opening another file or closing the model clears the Measurements list and its markers.
- Pressing `Esc` to stop measuring keeps the results on screen. Closing the Measurements
  panel hides the markers, and reopening it shows them again.
- Removing a result keeps the other rows as they were; older rows no longer fold up when a
  new result arrives.

### Internal
- The code follows the naming conventions in `CLAUDE.md` (adapted from Task Complete), checked
  by a clang-tidy job in CI. Renames only: no behavior changes.

### Fixed
- Measurement markers redraw at the right size after the window moves to a screen with
  different scaling.
- Release workflow: cancelling a tag run no longer publishes the release.

## [1.0.0] - 2026-11-15

First release: Windows installer and portable zip, Linux AppImage. The macOS DMGs are built
but held back until they pass a hands-on test on a real Mac (see `TODOS.md`).

### Added
- Open STEP AP203, AP214 and AP242 files, including gzip and zip compressed ones
  (`.stpz`, `.stp.gz`), with an open-file cache for fast reopening.
- Assembly tree, properties, hide / isolate / transparency.
- Measurement on the exact B-rep: entity info, distance, point to point with snapping,
  angle, radius / diameter, and mass properties; copy and CSV export.
- PMI: GD&T, dimensions and datums, saved views, and semantic dimension rendering with
  screen-space decluttering.
- Section view with up to three planes, caps and hatching.
- Image export and view preferences (background, lighting, antialiasing).
- About dialog with system info and the bundled license texts.
