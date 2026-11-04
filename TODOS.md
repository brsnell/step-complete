# TODOS

State as of 2026-11-03. M0–M4 are done and M5 (ship v1.0) is mostly done; see [docs/PLAN.md](docs/PLAN.md) §7. Packaging, the release workflow, file associations, the open-file cache and the user guide are on `main`. Left before tagging `v1.0.0`: hands-on checks on real hardware and signing.

## Release v1.0

### Manual test of the packages on real Windows and macOS machines

**What:** Install the Windows installer, the portable zip and both macOS DMGs (arm64, x64) on real machines with real GPUs. Open the NIST PMI models, measure, use a section view and saved views, and export an image. Then tag `v1.0.0`.

**Why:** CI renders with Mesa llvmpipe on Linux only. The viewport, PMI and measurement have never been run by hand on a real GPU, or on Windows/macOS at all.

**Context:** This also closes the open hands-on items in M2 (measurement on a real GPU, Windows/macOS) and M3 (PMI check on a real GPU). The release workflow runs on `v*` tags or by hand. The CI installer/DMG smoke tests only prove the packages install and start.

**Effort:** M
**Priority:** P1
**Depends on:** A Windows machine and a Mac to test on.

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

**Context:** Targets: 50 MB / ~1,000 parts interactive in ~30 s, and ≥ 15 fps orbiting 5M triangles on the reference machine. The benchmark model generator and import benchmark tools in `tools/` make this repeatable.

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
