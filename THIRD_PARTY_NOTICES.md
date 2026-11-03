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
| STIX Two Math (font, embedded in the executable) | SIL Open Font License 1.1 | https://github.com/stipub/stixfonts |

Full license texts are installed with the application (packaging, milestone M5) and are
also available from each project's source distribution.

## Bundled font
STIX Two Math (`resources/fonts/STIXTwoMath-Regular.ttf`, copyright 2001-2021 The STIX Fonts
Project Authors) draws the geometric-tolerance symbols of PMI labels (flatness, position,
maximum material condition and the like), which common system fonts lack. It is embedded
unmodified in the executable and extracted to the user's cache directory on first use. It is
licensed under the SIL Open Font License 1.1; the license text is in
`resources/fonts/OFL.txt`.

## Your rights under the LGPL
- You may replace the bundled Qt or OCCT libraries with compatible versions you build yourself.
- The exact source code for the bundled versions is available from the links above. The
  versions are pinned in `vcpkg.json` (OCCT) and `.github/workflows/ci.yml` (Qt).
