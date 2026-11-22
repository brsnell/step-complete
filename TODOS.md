# TODOS

State as of 2026-11-16. M0–M4 are done and M5 (ship v1.0) is mostly done; see [docs/PLAN.md](docs/PLAN.md) §7. Packaging, the release workflow, file associations, the open-file cache and the user guide are on `main`. The Windows packages passed a hands-on test on real hardware (2026-11-16). v1.0.0 ships for Windows and Linux only; the release workflow builds the macOS DMGs but does not publish them until the hands-on macOS check below passes.

Update 2026-11-21: v1.1 (M6) and v2 (M7) are drafted in [docs/PLAN.md](docs/PLAN.md) §7a, not approved yet. Every open item under Release and Viewport is blocked on hardware or a cost decision, so the v1.1 section is the next work that can be coded. Each v1.1 and v2 item gets a design in `docs/designs/` before work starts.

## Release

### Manual test of the packages on a real Mac

**What:** Install both macOS DMGs (arm64, x64) on a real Mac. Open the NIST PMI models, measure, use a section view and saved views, and export an image. Then publish the DMGs in the next release: add the `package-macos-*` download back to the `release` job in `.github/workflows/release.yml`, and drop the "not in v1.0" notes from `docs/user-guide.md` and `README.md`.

**Why:** CI renders with Mesa llvmpipe on Linux only. The viewport, PMI and measurement have never been run by hand on macOS.

**Context:** GitHub retired the `macos-14` runner image on 2026-11-02, so from then until 2026-11-17 every macOS job in CI and Release was cancelled before it started ("The configuration 'macos-14-arm64-us-default' is not supported"). Both workflows now use `macos-15`; the deployment target stays macOS 12 (set in `cmake/triplets/*-osx-sc.cmake`). Windows is done: the maintainer tested the Windows packages on a real machine with a real GPU on 2026-11-16, and they worked well. That closes the Windows and real-GPU parts of the open hands-on items in M2 (measurement) and M3 (PMI); only macOS remains. The release workflow runs on `v*` tags or by hand. The CI DMG smoke test only proves the DMGs install and start.

**Effort:** S
**Priority:** P1
**Depends on:** A Mac to test on (none available as of 2026-11-16).

### Signing certificates for Windows and macOS

**What:** Get an Apple Developer ID certificate and a Windows code-signing certificate, and add them as repository Actions secrets.

**Why:** Without them the packages are unsigned (Windows) or only ad-hoc signed (macOS), so SmartScreen warns and Gatekeeper blocks the app until the user overrides it.

**Context:** The workflow already signs and notarizes when the secrets exist ([packaging/README.md](packaging/README.md)): `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD`, `MACOS_SIGN_IDENTITY`, plus `MACOS_NOTARY_APPLE_ID`, `MACOS_NOTARY_TEAM_ID`, `MACOS_NOTARY_PASSWORD` for notarization; `WINDOWS_CERT_PFX_BASE64`, `WINDOWS_CERT_PASSWORD` for Windows. Buying them is a cost decision for the maintainer, not code. v1.0 can ship unsigned if that decision is to wait.

**Effort:** S (operator setup, not code)
**Priority:** P2
**Depends on:** A decision to pay for the certificates.

### Performance check on the 2016 reference machine

**What:** Measure open time and orbit frame rate against the §6 targets on a 2016-era machine (dual-core Skylake, Intel HD 520, 4 GB RAM), or a VM with an Intel iGPU passed through. A 2016 Intel MacBook Pro on macOS 12 covers the Mac side.

**Why:** The "runs well on 10-year-old machines" promise (PLAN §2a) is unverified. Every number in [docs/performance.md](docs/performance.md) comes from CI or a modern machine, and the rendering numbers come from software rendering.

**Context:** Targets: 50 MB / ~1,000 parts interactive in ~30 s, and ≥ 15 fps orbiting 5M triangles on the reference machine. A first hands-on data point: Hidden Line ran on an older laptop (Core i7-4600U, Intel HD 4400, 8 GB, Windows 10) on 2026-11-21, but nothing was timed. The benchmark model generator and import benchmark tools in `tools/` make this repeatable.

**Effort:** M
**Priority:** P2
**Depends on:** Access to the reference hardware.

## Viewport

### Check the macOS drawable choice on real hardware

**What:** On a real Mac, check whether binding OCCT to the widget's NSView (current) or to `window()->winId()` is the right choice.

**Why:** The current choice follows OCCT's Qt sample but has never run on Mac hardware. A wrong choice shows up as a black or misplaced viewport, or as broken compositing of the other widgets.

**Context:** `TODO(M0)` in `src/render/OcctViewport.cpp` (the drawable selection in context setup). Do this during the macOS package test above.

**Effort:** S
**Priority:** P2
**Depends on:** Manual macOS package test.

## v1.1 (M6)

### BOM / parts-list export

**What:** File > Export Parts List: a CSV with one row per unique part (quantity, name, product name, path), optionally with volume, mass and bounding box.

**Why:** A bill of materials is a common thing to pull out of a STEP assembly, and other viewers offer it.

**Context:** The assembly index in `src/core` already counts instances per part (shown in the Properties panel). Reuse the Measurements panel's CSV writing (UTF-8 with BOM). Mass columns use the part's material (`MainWindow::materialOf`: the user's, else the file's); parts without one leave the mass empty.

**Effort:** S
**Priority:** P2
**Depends on:** Design approved.

### Geometry validity check

**What:** Measure > Check Geometry: run OCCT's shape checker on the selected or visible parts and list the problems (invalid faces, open shells, gaps, tiny edges), each highlighted in 3D when selected.

**Why:** Bad geometry from the exporting CAD system is why a measurement, a mass property or a section cap can come out wrong. Today the user has no way to tell.

**Context:** `BRepCheck_Analyzer` reports per sub-shape status; `ShapeAnalysis` finds open shells and small edges. Run it off the GUI thread with progress and cancel, since large assemblies take time. The Import Messages panel holds translator messages only; the results probably belong in a panel of their own.

**Effort:** M
**Priority:** P2
**Depends on:** Design approved.

## v2 (M7)

### Clash and interference detection

**What:** Find parts that intersect or touch, with an optional clearance distance. List each pair with its overlap volume or minimum gap, and highlight the pair in 3D when selected.

**Why:** Checking an assembly for interference is a common review task that other viewers offer.

**Context:** Pairs grow with the square of the part count, so start with a bounding-box pass and check exact geometry only for the pairs that overlap (`BRepExtrema_ShapeProximity` for touching and gaps, a Boolean common for overlap volume). Must work on transformed instances and run in the background with progress and cancel.

**Effort:** L
**Priority:** P2
**Depends on:** Design approved.

### Wall thickness analysis

**What:** Color the model by local wall thickness with a min/max legend and threshold, plus a pick readout of the thickness at a point.

**Why:** Listed as a v2 candidate (PLAN §1). Used to check castings, moldings and printed parts.

**Context:** Read-only analysis, so it fits the current design best of the v2 items. Choose between exact B-rep thickness (accurate, slow) and ray casting on the display mesh (fast, approximate) in the design. Either way it must stay usable on the PLAN §2a reference machine: run it in the background with progress and cancel.

**Effort:** L
**Priority:** P2
**Depends on:** Design approved.

### Other import formats

**What:** Open IGES first, then mesh formats (STL, OBJ, glTF).

**Why:** People often get IGES or mesh files instead of STEP, and other viewers open them.

**Context:** OCCT's `DE_Wrapper` reads all of them. IGES has B-rep geometry, so measurement works. Mesh files have no exact geometry, so measurement has to fall back to the mesh or be turned off for them, and the UI must say which. The importer, the open-file cache, file associations and the Open dialog's filters all assume STEP today. PMI stays AP242 only. This changes a v1 non-goal (PLAN §1).

**Effort:** M
**Priority:** P3
**Depends on:** Design approved.

### Model comparison

**What:** Open two revisions of a model, align them, and color what was added, removed or changed, with a deviation legend and a list of changed parts.

**Why:** Listed as a v2 candidate (PLAN §1). Checking what changed between revisions is a frequent review task.

**Context:** Needs two documents in one scene (`SceneManager` and `MainWindow` hold one today), a way to pair parts between revisions (by name, by product, by geometry), and a deviation method (point sampling against the other shape, or Boolean differences).

**Effort:** L
**Priority:** P3
**Depends on:** Design approved.

### Markup

**What:** 3D notes and simple sketches pinned to geometry, saved to a sidecar file next to the model and included in image export.

**Why:** Listed as a v2 candidate (PLAN §1). Reviewers need to point at things.

**Context:** It must never write into the STEP file. Notes have to stay attached when the same file is reopened, so they need a stable reference to the geometry (assembly path plus face). Store it per file with `ModelDataStore` (as user saved views do), under its own key; a shareable export can sit on top of the same JSON.

**Effort:** L
**Priority:** P3
**Depends on:** Design approved.

## Deferred

### Bundled Mesa `opengl32.dll` for software rendering on Windows

**What:** Ship Mesa's `opengl32.dll` as a fallback for Windows machines without a working GL driver (VMs, RDP, old drivers).

**Why:** Without a GL 3.3+ driver the app cannot render on Windows.

**Context:** OCCT links `opengl32` directly, so the fallback needs a delay-loaded OCCT GL toolkit. It cannot be tested here. Deferred in M5.

**Effort:** M
**Priority:** P3
**Depends on:** Bug reports from users who hit it.

### Crash reporting

**What:** Collect crash reports (minidumps or similar) from the shipped app.

**Why:** Field crashes on GPUs and drivers that CI never sees will otherwise go unreported or arrive without a stack.

**Context:** Deferred in M5. The About dialog's copyable system info is the bug-report path for now.

**Effort:** M
**Priority:** P3
**Depends on:** v1.0 in users' hands.

### Flathub package

**What:** Publish on Flathub alongside the AppImage.

**Why:** It's the normal way Linux users find and update desktop apps.

**Context:** Deferred in M5.

**Effort:** M
**Priority:** P4
**Depends on:** v1.0 released.

### Coarse-then-fine first mesh

**What:** Show a coarse mesh right after import, then refine it in the background.

**Why:** A model shows up sooner the first time it's opened. Re-opens already take seconds thanks to the open-file cache.

**Context:** Deferred in M5. Parallel meshing (2.6× on 4 cores) already landed.

**Effort:** M
**Priority:** P4
**Depends on:** Reference-machine numbers showing first-open time misses the §6 target.

### Exploded view

**What:** Move assembly parts apart along computed directions.

**Why:** A common viewer feature for understanding assemblies.

**Context:** Deferred in M4. Moving parts would detach their PMI and break measurement picks unless every subsystem learned about the offsets, so it isn't worth the extra weight now.

**Effort:** L
**Priority:** P4
**Depends on:** User demand.

### Measurements that follow section views

**What:** Make measurement results and picks respect the active section planes (no picks on the clipped-away side, results that make sense against the capped faces).

**Why:** It's left open in M2 ("Results that follow section views").

**Context:** The section view clips the parts only; witnesses stay whole.

**Effort:** M
**Priority:** P3
**Depends on:** Real use that shows it's a problem.
