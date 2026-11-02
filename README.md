# STEP Complete

A fast, lightweight, cross-platform viewer for STEP CAD files (AP203 / AP214 / AP242), with
accurate measurement and PMI (Product Manufacturing Information: GD&T, dimensions, datums,
saved views).

Built with C++17, [Open CASCADE Technology](https://github.com/Open-Cascade-SAS/OCCT) and
[Qt 6](https://www.qt.io). MIT licensed.

> **Status: early development (milestone M0).** The 3D viewer foundation works; STEP import is
> next. See [docs/PLAN.md](docs/PLAN.md) for the roadmap.

## Goals
- **Accurate measurement** on exact B-rep geometry, never the display mesh.
- **Full AP242 PMI**: semantic and graphical annotations, saved views, links to geometry.
- **Light enough for 10-year-old hardware**: a 2016 laptop with integrated graphics and 4 GB RAM.
- **Windows 10+, macOS 12+ (Intel and Apple Silicon), Linux.**

## Building

### Linux, quick start with distro packages (Ubuntu 24.04)
Uses OCCT 7.6 and Qt 6.4 from Ubuntu. Releases use OCCT 8.0 and Qt 6.8 LTS, and the code
supports both.

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev libgl-dev libgtest-dev libtbb-dev \
  libocct-data-exchange-dev libocct-visualization-dev libocct-ocaf-dev
```

```bash
cmake --preset dev
```

```bash
cmake --build --preset dev
```

```bash
ctest --preset dev
```

```bash
./build/dev/src/app/StepComplete
```

### All platforms, release configuration (OCCT 8 via vcpkg)
1. Install [vcpkg](https://github.com/microsoft/vcpkg) and set `VCPKG_ROOT`.
2. Install Qt 6.8 LTS (official binaries) and put it on `CMAKE_PREFIX_PATH`.
3. Configure and build:

```bash
cmake --preset vcpkg -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.x/<platform>
```

```bash
cmake --build --preset vcpkg
```

The first build compiles OCCT, which takes a while. vcpkg caches it afterwards.

## Running on old or unusual graphics
- Needs OpenGL 2.1 or newer (3.3+ recommended; every 2016-era GPU qualifies).
- **Linux:** `StepComplete --software-gl` uses Mesa's software renderer if the GPU driver is broken.
- **Linux/Wayland:** runs through XWayland automatically, because OCCT's Linux renderer uses GLX.
- **Help → Graphics Information** shows what the driver reported. Include it in bug reports.

## Controls
| Action | Mouse / key |
|---|---|
| Orbit | Left drag |
| Pan | Middle drag, or Shift + left drag |
| Zoom | Wheel (zooms at the cursor), or Ctrl + left drag |
| Select | Left click |
| Fit all | `F` |
| Standard views | `1`–`6`, isometric `0` |
| Perspective toggle | `P` |
| Shaded / with edges / wireframe | `S` / `E` / `W` |

## Repository layout
```
src/core     OCCT-only logic (no Qt): import, units, measurement, PMI model
src/render   Qt <-> OCCT viewport bridge and presentations
src/app      Qt Widgets application
tests/       GoogleTest (core) and Qt Test (render smoke tests)
docs/        Plan and design notes
```

## License
MIT. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
