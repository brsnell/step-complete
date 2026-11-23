# IGES import

Status: APPROVED (2026-11-23)

## What

STEP Complete opens IGES files (`.igs`, `.iges`) the way it opens STEP: **File > Open**, Open
Recent, drag and drop, the command line and the file associations. Parts keep their names, colors
and layers, the file's unit is shown, and everything that works on exact geometry (measurement,
mass properties, sections, the geometry and clash checks, wall thickness, exports) works on them.

## Why

People are often sent IGES instead of STEP, and other viewers open it (TODOS v1.2, PLAN §1 lists
"other formats" as a v1 non-goal planned for later). IGES carries B-rep geometry, so it fits the
viewer without a fallback; mesh formats (STL, OBJ, glTF) do not, and come later.

## Decisions

1. **One importer, the format from the content.** `importModel` (the former `importStep`, which
   stays as an alias) looks at the file's first bytes: a STEP file starts with `ISO-10303-21`, an
   IGES file is made of 80-column records with a section letter in column 73. A file named `.stp`
   that holds IGES opens as IGES, and the reverse. Only when the content says neither does the
   extension decide (`FileFormat.h`); a file that is neither fails with "The file is not a STEP
   or IGES file." Gzip and zip containers are inflated first, as for STEP, so `.igs.gz` works.
2. **OCCT's XCAF IGES reader** (`IGESCAFControl_Reader`) with names, colors and layers. IGES has
   no product structure: each top-level entity becomes a part at the top of the tree. PMI and
   saved views are STEP-only; the PMI panel stays empty.
3. **Units.** The global section's unit flag (inch, mm, ft, m, µm, cm; a named unit for flag 3)
   becomes the file unit shown in the status bar and Properties. OCCT converts the geometry to
   millimetres, like STEP.
4. **Everything else is shared.** Meshing, the open-file cache (the cache key is the content, so a
   cached IGES file stays IGES), PMI-free documents and every tool work unchanged.
   `Document::format()` records what was read; Properties shows it as **Format**.
5. **Associations.** IGES joins "Open with" on every platform: a Windows ProgID with
   `OpenWithProgids` (the optional "default program" task stays STEP-only, so installing never
   takes over `.igs`), a macOS document type with an imported type declaration (another app's
   declaration wins), and `model/iges` in the Linux desktop entry and MIME supplement.
6. **Toolkits.** `TKDEIGES` (OCCT ≥ 7.8) or `TKIGES` and `TKXDEIGES` (7.6, 7.7). vcpkg's OCCT
   already builds them, so `vcpkg.json` does not change.

## Out of scope

- Mesh formats (STL, OBJ, glTF): next, with a mesh fallback for measuring (TODOS v1.2).
- Sewing surface-only IGES files (trimmed surfaces with no solids) into solids. Their faces are
  shown and measurable, but volume and mass need closed shells; the geometry check reports the
  gaps. Revisit if users send such files.
- Writing IGES.

## Tests

- `core.IgesImport.*`:
  - two named, colored solids round-trip with their names, colors, placement and unit;
  - an inch file comes back in millimetres with the inch unit;
  - the content decides the format (IGES named `.stp`, STEP named `.igs`), STEP after IGES in
    one process keeps its structure and colors, and a text file is rejected;
  - a gzipped IGES file opens;
  - the cache keeps the format.
- `core.FileFormat.*`: extensions, display names and the unit flags.
- `app.main_window`: an IGES file opens, and Properties shows the format.
