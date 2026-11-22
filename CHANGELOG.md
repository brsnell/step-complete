# Changelog

All notable changes to STEP Complete. The version lives in `VERSION`; a release is the
tag `v<VERSION>`, which builds the packages and publishes a GitHub Release.

## [Unreleased]

### Added
- View > Saved Views: save the camera, the hidden parts and the section planes under a name
  (`Ctrl+Shift+V`), restore them from the list, and rename, delete or reorder them. Views are
  kept per file content in the app's data folder, so they survive copies, renames and re-exports.
- Section planes at any angle: each plane's axis list has **Custom**, with X, Y and Z fields for
  the plane's direction, and a **Pick Face** button puts the plane on a clicked flat face
  (facing out of the part; `Esc` cancels).
- File > Export Geometry: saves the visible or selected parts as STL (one file, or one file per
  unique part), OBJ with colors, or glTF/GLB with colors and the assembly structure. Mesh
  quality As displayed, Fine, Extra fine or a custom tolerance; finer meshes are built on a copy,
  so the open model is untouched. Unit choice for STL and OBJ, and Y up (default) or Z up for OBJ.
  Runs in the background with a progress dialog and Cancel.
- Assembly panel: a filter box above the tree (View > Find Part..., `Ctrl+F`) shows only the
  parts and assemblies whose name or product name contains the text, expanded to the matches.
- View > Hidden Line (`L`): a display style with white faces and dark sharp edges and outlines,
  with edges behind faces hidden. Part transparency and section caps work in it.
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
- CI keeps main's vcpkg caches warm: CI also runs on main on Mondays and Thursdays, the Windows
  vcpkg-Qt check runs on main (for dependency changes and weekly), and runs on main are no longer
  cancelled by a newer push. A pull request can only use caches from its own branch or main, and
  an evicted OCCT build cost every job 50 to 75 minutes.
- Release builds enable OCCT's `rapidjson` feature (vcpkg), which its glTF writer needs. RapidJSON
  (MIT) is listed in `THIRD_PARTY_NOTICES.md` and Help > About > Licenses.
- The code follows the naming conventions in `CLAUDE.md` (adapted from Task Complete), checked
  by a clang-tidy job in CI. Renames only: no behavior changes.
- CI and release workflows run on Node 24: `checkout` v7, `cache` v6, `upload-artifact` v7,
  `download-artifact` v8, `action-gh-release` v3. The MSVC setup is a small local action
  (`.github/actions/msvc-env`) in place of `ilammy/msvc-dev-cmd`, which has no Node 24 release.

### Fixed
- Measurement markers redraw at the right size after the window moves to a screen with
  different scaling.
- PMI annotations (lines, arrowheads, labels) redraw at the right size after the window moves
  to a screen with different scaling, keeping which items are shown, the selection and the
  semantic-labels setting.
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
