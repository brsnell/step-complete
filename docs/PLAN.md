# STEP Complete: Project Plan

A cross-platform (Windows / macOS / Linux) desktop viewer for STEP (AP203/AP214/AP242)
with serious measurement and PMI (Product Manufacturing Information) support.

**Stack:** C++17 · Open CASCADE Technology (OCCT) 8.0.x · Qt 6.8 LTS · CMake · vcpkg

**License:** MIT · **Repo:** github.com/brsnell/step-complete

**Status:** M0 (foundations) is implemented. The Qt/OCCT viewport works and is covered by render
tests; CI runs once the repo is on GitHub.

---

## 1. Goals and non-goals

### Goals (v1.0)
- Open large STEP assemblies reliably, with progress and cancel, without freezing the UI.
- Faithful display: assembly structure, names, colors, layers, transparency.
- Navigation: orbit/pan/zoom, view cube, standard views, fit, perspective/ortho.
- Visualization tools: hide/isolate/show-only, transparency, section planes with capping, display
  modes (shaded, shaded+edges, wireframe, hidden-line).
- **Measurement:** entity info (point, edge, face, solid), distances (min, center-to-center,
  point-to-point, along axis), angles, radius/diameter, edge length, face area, volume/mass
  properties, bounding box. Correct units and documented precision.
- **PMI:** AP242 semantic PMI (dimensions, geometric tolerances, datums, datum targets), graphical
  PMI (polyline/tessellated presentation), saved views, cross-highlighting between PMI and the
  geometry it applies to.
- Native-feeling app on all three platforms, packaged installers, file associations.

### Non-goals (v1.0)
- Editing geometry or PMI, writing STEP. (Read-only viewer; keeps scope and risk down.)
- Other formats besides STEP. The architecture keeps the door open (OCCT's `DE_Wrapper` handles
  IGES, glTF, OBJ, STL, VRML), but we won't polish them for v1.
- Model comparison, wall-thickness analysis, markup and collaboration. Candidates for v2.

---

## 2. Key technology decisions

| Area | Decision | Why |
|---|---|---|
| Geometry kernel | **OCCT 8.0.x** | Released after 7.9; C++17 baseline, improved vcpkg support. Pin an exact patch version. |
| Document model | **XCAF (OCAF) document** via `STEPCAFControl_Reader` | The only OCCT path that keeps names, colors, layers, assembly structure, GD&T and saved views. |
| UI toolkit | **Qt 6.8 LTS** (Widgets, not QML) | Widgets are lighter than QML and faster to build well for docks, trees and tables. 6.8 still supports macOS 12. Release builds use Qt's official binaries; vcpkg's Qt (6.11) is newer than the baseline. |
| 3D integration | **`QOpenGLWidget` + OCCT wrapping Qt's GL context** (`Aspect_NeutralWindow`, `OpenGl_Context::Init` on the existing context, render into Qt's FBO) | The older approach (native window handle + `WA_PaintOnScreen`) misbehaves with HiDPI and fights docking and overlays. **Linux:** OCCT's driver uses GLX, so we force Qt's xcb platform and run under XWayland on Wayland desktops. Native Wayland needs an EGL build of OCCT, a post-1.0 item. |
| Build | **CMake + vcpkg manifest mode** for OCCT 8.0.1 (`vcpkg.json`, `CMakePresets.json`); Qt from official binaries | One OCCT recipe for all 3 platforms. A `dev` preset builds against distro packages (Ubuntu 24.04: OCCT 7.6, Qt 6.4), so the code supports OCCT ≥ 7.6 and Qt ≥ 6.4. |
| Threading | Import and meshing on worker threads (`QtConcurrent`), OCCT parallel meshing (`BRepMesh_IncrementalMesh`, parallel on), a `Message_ProgressIndicator` subclass that sends Qt signals and supports cancel | Large STEP files take seconds to minutes to parse; the UI must stay responsive. |
| Testing | GoogleTest for core, Qt Test for UI logic, offscreen golden-image tests (Mesa llvmpipe in CI) | Measurement and PMI correctness must be guarded by regression tests. |
| Licensing | **MIT** for our code; OCCT (LGPL 2.1 + exception) and Qt (LGPL 3) **dynamically linked** | Permissive and simple. Dynamic linking plus `THIRD_PARTY_NOTICES.md` meets the LGPL obligations. |

---

## 2a. Hardware baseline: runs well on 10-year-old machines

**Reference low-end machine (2016):** dual-core Intel Skylake laptop, Intel HD 520 integrated
graphics, 4 GB RAM, HDD or SATA SSD. Also a 2016 MacBook Pro (Intel) on macOS 12.

| Platform | Minimum |
|---|---|
| Windows | 10 (64-bit) |
| macOS | 12 Monterey, **Intel and Apple Silicon** builds |
| Linux | glibc 2.35 (Ubuntu 22.04 era), X11 or Wayland (via XWayland) |
| OpenGL | 2.1 hard minimum (OCCT), 3.3+ recommended; macOS 4.1 core |

Design rules that follow from this:
- **Small dependency footprint:** Qt Core/Gui/Widgets/OpenGLWidgets only (no QML, WebEngine or
  Network). OCCT built with FreeType only (no TBB, FreeImage or VTK); OCCT's own thread pool
  handles parallel work.
- **Cheap rendering defaults:** MSAA off (optional toggle), no shadows, ray tracing or PBR,
  compatibility GL profile for the widest driver support. Redraw only on demand. Hover highlight
  redraws only the immediate layer, never the whole scene.
- **Memory:** a 50 MB STEP file should peak under 2 GB and sit under 1 GB after load on a 4 GB
  machine. Instancing (§3) is mandatory, and the STEP reader's model is freed after transfer.
- **Navigation on big models:** small-object culling while orbiting (`Graphic3d_ZLayerSettings`
  culling size), deflection scaled to model size.
- **Fallback:** `--software-gl` (Mesa llvmpipe) on Linux now. A bundled Mesa `opengl32.dll`
  on Windows in M5.
- **Targets:** cold start < 2 s on an HDD, idle RAM < 150 MB, install size < 100 MB.
- **Tested:** render tests run on Mesa's software renderer in CI, a stricter bar than any
  real 2016 GPU.

---

## 3. Architecture

Three layers with dependencies pointing one way. The core has no Qt dependency, so it can be unit
tested headless and reused later (CLI tools, batch PMI reports).

```
┌──────────────────────────────────────────────────────────────┐
│ app/      Qt Widgets: MainWindow, docks, tools, settings      │
│           AssemblyTreeModel, PmiTreeModel, PropertiesPanel,   │
│           MeasureToolController, SectionToolController        │
├──────────────────────────────────────────────────────────────┤
│ render/   OCCT visualization + Qt GL bridge                   │
│           OcctViewport (QOpenGLWidget), SceneManager,         │
│           PmiPresentation, MeasurePresentation, ViewCube,     │
│           SelectionManager, ClipPlaneManager                  │
├──────────────────────────────────────────────────────────────┤
│ core/     OCCT only, no Qt                                     │
│           Document (XCAF wrapper), StepImporter, AssemblyIndex,│
│           PmiExtractor → PmiModel, Measure (pure functions),  │
│           Units, MassProps, Metadata                          │
└──────────────────────────────────────────────────────────────┘
```

### Repository layout
```
step-complete/
  CMakeLists.txt  CMakePresets.json  vcpkg.json
  src/core/   src/render/   src/app/
  resources/  (icons, fonts for GD&T symbols, translations)
  tests/core/  tests/render/  tests/data/ (fetch script, not checked-in models)
  packaging/  (windows/, macos/, linux/)
  .github/workflows/ci.yml
```

### Core components
- **`StepImporter`**: configures `STEPCAFControl_Reader` with color, name, layer, props, GD&T,
  SHUO, view and metadata modes on. Reads into a fresh `TDocStd_Document` (XmlXCAF/BinXCAF
  formats registered). Handles the length unit (`XCAFDoc_DocumentTool::SetLengthUnit`), reports
  progress, supports cancel, and collects translator warnings to show the user ("12 entities
  skipped").
- **`AssemblyIndex`**: flattens the XCAF label graph using `XCAFPrs_DocumentExplorer` into stable
  node IDs (instance path → ID). Everything else (tree, selection, PMI links) refers to these IDs,
  never to raw `TDF_Label`s from the UI.
- **`PmiExtractor` → `PmiModel`**: walks `XCAFDoc_DimTolTool` (dimensions, geometric tolerances,
  datums), `XCAFDoc_ViewTool` (saved views) and `XCAFDoc_NotesTool`. Produces plain value objects
  (type, values, tolerance, modifiers, datum references, attached shapes, presentation polylines,
  annotation plane, text position). The UI and renderer depend on `PmiModel`, not on XCAF.
- **`Measure`**: stateless functions that take `TopoDS_Shape`s plus transforms and return results
  with units and precision. Built on `BRepExtrema_DistShapeShape`, `BRepGProp`,
  `BRepAdaptor_Curve/Surface` (to detect lines, circles, planes, cylinders, cones, spheres) and
  `Bnd_OBB`. This is the most test-heavy part of the codebase.

### Render components
- **`OcctViewport`**: `QOpenGLWidget` that owns `V3d_Viewer`, `V3d_View`, `AIS_InteractiveContext`.
  Handles device pixel ratio (Retina/HiDPI), maps Qt mouse/touch/gesture events to
  `AIS_ViewController`, and redraws on demand only (no continuous loop).
- **`SceneManager`**: builds presentations from the document. Uses `XCAFPrs_AISObject` for
  colored shapes. **Instancing:** one presentation per prototype (part), placed with
  `AIS_ConnectedInteractive` per instance, so a bolt used 500 times is meshed once.
- **`PmiPresentation`**: custom `AIS_InteractiveObject`s that render
  1. *graphical PMI*: the polylines/tessellation from the file, drawn as-is (most faithful), and
  2. *semantic-only PMI*: our own generated annotation (dimension lines, arrows, text, feature
     control frames with GD&T symbols) when the file has no presentation.
  Text uses `Graphic3d_Text` with a bundled font that covers the GD&T Unicode symbols
  (⌀ ⏥ ⌭ ⟂ ∥ ∠ ⌖ ⌒ ⌓ ↗ ⌰ Ⓜ Ⓛ Ⓕ Ⓟ …).
- **`MeasurePresentation`**: measurement overlays using `PrsDim_*` (length, angle, radius,
  diameter dimensions) plus custom labels. Always on top, screen-space text.

### App
- Dockable panels: **Assembly**, **PMI**, **Saved Views**, **Properties**, **Measurements**.
- Selection is synchronized everywhere: picking in 3D selects in the tree and the reverse, and
  selecting PMI highlights the geometry it applies to.
- Command pattern for view-state changes (visibility, isolate, section), which gives undo for
  view operations.

---

## 4. Measurement design (the "serious" part)

Credibility rules:
1. **Measure the exact B-rep, never the mesh.** The mesh is for display and picking only. After
   picking, map back to the exact `TopoDS_Face/Edge/Vertex` and compute on that.
2. **Respect instance transforms.** Every measurement is done in world coordinates with the full
   assembly location applied.
3. **Units:** store everything internally in mm (OCCT convention once the unit is set). Show
   values in the user's chosen unit, and show the file's source unit.
4. **Precision:** set displayed decimals per user preference. Expose the geometric tolerance of
   the entities involved, so users can see when a result is at the noise floor.
5. **Show the work:** each result draws its witness geometry (closest points, axes, normals) in
   3D so users can verify what was measured.

### Measurement modes
| Mode | Picks | Output |
|---|---|---|
| Entity info | any 1 | Vertex XYZ; edge type/length/radius/center; face type/area/normal/radius/axis |
| Distance | 2 entities | Minimum distance + ΔX/ΔY/ΔZ; center-to-center for circles/cylinders/spheres; parallel-plane distance for parallel planar faces |
| Point-to-point | 2 points (snapped: vertex, edge midpoint, arc center, face point) | Distance + deltas |
| Angle | 2 lines/planes/axes | Angle (and supplement), with 3D arc |
| Radius/diameter | circular edge / cylindrical or spherical face | R, ⌀, center, axis |
| Mass properties | solid(s) / selection | Volume, area, centroid, inertia tensor, bounding box (axis-aligned and oriented). Compare against the file's STEP validation properties when present. |

UX details: pre-highlight on hover, snap markers, a pinned result list (copy to clipboard, export
CSV), results that survive hide/show and stay in sync with section views.

---

## 5. PMI design

What OCCT gives us (via `STEPCAFControl_Reader` with GD&T mode on):
- Semantic dimensions (`XCAFDimTolObjects_DimensionObject`): type, value, tolerance,
  limits/fits class, modifiers, qualifier, presentation, plane, text position.
- Geometric tolerances (`XCAFDimTolObjects_GeomToleranceObject`): characteristic, value, zone
  modifiers, material modifiers, datum system.
- Datums and datum targets (`XCAFDimTolObjects_DatumObject`).
- Saved views (`XCAFView_Object`): camera, clipping, which shapes and PMI are visible.
- Links from PMI to geometry (the faces/edges each annotation applies to).

Features:
- PMI tree grouped by type and by saved view. Filter by type. Search.
- Activating a saved view animates the camera and applies its visibility and clipping.
- Selecting PMI highlights its target geometry (and the reverse: "show PMI for this face").
- Toggle graphical vs. semantic rendering when both exist.
- Properties panel shows the full semantic content of the selected PMI item, including fields
  that aren't rendered.

**Known risk:** OCCT's AP242 PMI coverage is good but not complete. Some constructs come through
partially (for example some datum-target types, unusual modifier combinations and some
tessellated-annotation variants). Mitigation:
- Validate early against the **NIST MBE PMI test models** (CTC/FTC, public domain) and CAx-IF
  AP242 test files, with expected counts per file in the tests.
- Keep a coverage matrix in `docs/pmi-coverage.md`.
- If a gap matters, patch it locally behind a vcpkg overlay port and upstream the fix to OCCT.
- Show unsupported PMI as "present but not interpreted" instead of hiding it.

---

## 6. Performance targets

| Scenario | Target |
|---|---|
| 50 MB STEP, ~1,000 parts | Interactive within ~10 s on a modern laptop, ~30 s on the 2016 reference machine; UI never blocks |
| Orbit on 5M triangles | ≥ 30 fps on a modern iGPU; ≥ 15 fps on the 2016 reference machine (culling while moving) |
| Re-opening a file | Near-instant via a cache (BinXCAF document + triangulation, keyed by file hash) |

Techniques: instancing (§3), parallel meshing, deflection tuned to model size, coarse first pass
then refinement, frustum culling (built into OCCT), lazy tree model, and a cancellable import.

---

## 7. Milestones

Each milestone ends with something runnable on all three platforms in CI.

### M0: Foundations (≈1–2 weeks): ✅ implemented, CI pending first push
- Repo, CMake presets, vcpkg manifest (OCCT 8.0.x with freetype; Qt base + opengl), pinned
  baseline.
- GitHub Actions: Windows/MSVC, macOS/arm64, Ubuntu/GCC. Cache vcpkg binaries (the first OCCT
  build is slow).
- `OcctViewport` showing a demo part with view cube, orbit/pan/zoom and HiDPI handling, on X11
  and XWayland.
- Render smoke tests (Xvfb + llvmpipe): rendering, resize, display styles.
- **Exit:** the same binary behavior on all 3 OSes. This milestone proves the riskiest
  integration (Qt + OCCT GL) first.

### M1: Open and view (≈3–4 weeks)
- `StepImporter` with progress/cancel on a worker thread; warnings panel.
- `AssemblyIndex`, assembly tree, names, colors, layers, instancing.
- Selection and highlight synced between tree and 3D; Properties panel; metadata.
- Display modes, hide/isolate/transparency, standard views, fit, perspective/ortho.
- Recent files, drag-and-drop, command-line open.
- **Exit:** opens the NIST test models and a large real-world assembly correctly.

### M2: Measurement (≈3–4 weeks)
- `core/Measure` with full unit tests (analytic shapes with known answers).
- Snapping and pre-highlight; all modes in §4; witness graphics; results list; units/precision
  preferences; mass properties with validation-property comparison.
- **Exit:** test suite covers every mode across transformed instances.

### M3: PMI (≈4–6 weeks)
- `PmiExtractor`/`PmiModel` with tests against NIST models (expected counts and values).
- Graphical PMI rendering; saved views; PMI tree; cross-highlighting.
- Semantic-only rendering: dimension geometry, feature control frames, datum flags.
- PMI coverage matrix document.
- **Exit:** NIST CTC/FTC models show all annotations, saved views work, and gaps are documented.

### M4: Visualization tools (≈2 weeks)
- Section planes (up to 3, capping, hatching, drag handles), exploded view (optional),
  screenshot export, background/lighting preferences.

### M5: Ship v1.0 (≈2–3 weeks)
- Performance pass against §6 targets; open-file cache.
- Packaging: Windows (windeployqt + MSIX or WiX, code signing), macOS (macdeployqt, DMG,
  signing + notarization), Linux (AppImage first, Flathub later). File associations
  `.step/.stp/.stpz`.
- Crash reporting (optional), about/licenses dialog (required for LGPL), user docs.

**Total:** about 4–5 months for one experienced developer. M2 and M3 can run in parallel with two
people.

---

## 8. Testing strategy
- **Core unit tests** (GoogleTest), headless: importer, assembly index, measurement, PMI
  extraction.
- **Test corpus:** script in `tests/data/fetch.sh` downloads the NIST MBE PMI models, CAx-IF
  samples and generated fixtures. Not committed to git (size).
- **Render tests:** offscreen `V3d_View` dumps compared to golden images with a tolerance, run in
  CI under Mesa llvmpipe.
- **UI smoke tests** (Qt Test): open file → select → measure → activate a saved view.
- Sanitizers (ASan/UBSan) on the Linux CI job; clang-tidy and clang-format enforced.

---

## 9. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Qt ↔ OCCT OpenGL integration (Wayland, HiDPI, macOS) | High | Solve first in M0 with the `QOpenGLWidget` approach |
| macOS OpenGL deprecation | Medium (long-term) | Still works (GL 4.1 core). OCCT has no Metal backend; monitor it. |
| Gaps in AP242 PMI support | High for the PMI promise | NIST test models early, coverage matrix, patch and upstream |
| Large-file memory and time | Medium | Instancing, parallel meshing, cache, cancel |
| OCCT 8.0 is a fresh major release | Low–medium | Pin a patch version, track 8.0.x maintenance releases |
| vcpkg cold builds of OCCT + Qt in CI | Low (CI time) | Binary caching |

---

## 10. Decisions (resolved)
1. **License:** open source, **MIT**.
2. **Name:** **STEP Complete** (`step-complete`).
3. **Order after M1:** **measurement (M2) first**, then PMI (M3).
4. **Platform minimums:** hardware from 10 years ago; see §2a.
5. **Hosting:** GitHub, `brsnell/step-complete`.

---

## 11. Next steps
1. Push to GitHub and get CI green on Windows, macOS (arm64 and x64) and Linux. Enable
   warnings-as-errors on MSVC and Apple Clang once clean.
2. M1: `StepImporter` (background thread, progress, cancel), `AssemblyIndex`, assembly tree,
   instancing, selection sync.
3. Get a 2016-era test machine (or VM with an Intel iGPU passthrough) for manual performance
   checks against §2a and §6.
