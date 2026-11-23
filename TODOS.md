# TODOS

State as of 2026-11-16. M0–M4 are done and M5 (ship v1.0) is mostly done; see [docs/PLAN.md](docs/PLAN.md) §7. Packaging, the release workflow, file associations, the open-file cache and the user guide are on `main`. The Windows packages passed a hands-on test on real hardware (2026-11-16). v1.0.0 ships for Windows and Linux only; the release workflow builds the macOS DMGs but does not publish them until the hands-on macOS check below passes.

Update 2026-11-23: v1.1 (M6), v1.2 (M7) and v1.3 (M8, markup) shipped (the v1.2 milestone was drafted as "v2"; it shipped as 1.2 because it only adds to the viewer). See [docs/PLAN.md](docs/PLAN.md) §7a. Every open item under Release and Viewport is blocked on hardware or a cost decision. Each item gets a design in `docs/designs/` before work starts.

## Release

### Manual test of the packages on a real Mac

**What:** Install both macOS DMGs (arm64, x64) on a real Mac. Open the NIST PMI models, measure, use a section view and saved views, and export an image. Then publish the DMGs in the next release: add the `package-macos-*` download back to the `release` job in `.github/workflows/release.yml`, and drop the "not published yet" notes from `docs/user-guide.md` and `README.md`.

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

Shipped as v1.1.0 on 2026-11-22 (see `CHANGELOG.md`).

## v1.2 (M7)

Shipped as v1.2.0 on 2026-11-23 (see `CHANGELOG.md`).

## v1.3 (M8)

Shipped as v1.3.0 on 2026-11-23 with markup ([design](docs/designs/markup.md), `CHANGELOG.md`).
Moving a note's box, shapes and colors of one's own, and replies or statuses are out of its
scope; add them here if users ask.

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
