# Third-party software

STEP Complete itself is MIT-licensed (see `LICENSE`). Binary distributions include the
libraries below. Each is **dynamically linked**, so users can replace it with their own build,
as the LGPL requires.

| Component | License | Source |
|---|---|---|
| Open CASCADE Technology (OCCT) | LGPL 2.1 with Open CASCADE exception | https://github.com/Open-Cascade-SAS/OCCT |
| Qt 6 (Core, Gui, Widgets, OpenGLWidgets) | LGPL 3.0 | https://download.qt.io/official_releases/qt/ |
| FreeType (used by OCCT for text) | FreeType License (FTL) | https://freetype.org |
| Fontconfig (Linux, used by OCCT) | MIT-style | https://www.freedesktop.org/wiki/Software/fontconfig/ |

Full license texts are installed with the application (packaging, milestone M5) and are
also available from each project's source distribution.

## Your rights under the LGPL
- You may replace the bundled Qt or OCCT libraries with compatible versions you build yourself.
- The exact source code for the bundled versions is available from the links above. The
  versions are pinned in `vcpkg.json` (OCCT) and `.github/workflows/ci.yml` (Qt).
