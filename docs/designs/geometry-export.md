# Geometry export (STL, OBJ, glTF)

Status: APPROVED (2026-11-21)

## What

**File > Export Geometry...**, next to Export Image. It writes the visible parts, or the selected
parts, as:

- **STL** (binary): one merged file, or one file per unique part;
- **OBJ** with a `.mtl` file for colors;
- **glTF** (`.gltf` + `.bin`) or **GLB** (one file), with colors and the assembly structure.

A dialog picks the format, the parts, the mesh quality and (STL and OBJ) the unit. The export
runs off the GUI thread with a progress dialog and Cancel.

## Why

"I need an STL for my 3D printer" or "a glTF for the web" is a common reason to open a STEP
viewer, and other viewers offer it (TODOS v1.1). Writing STEP stays a non-goal (PLAN §1).

## Decisions

1. **OCCT's writers where they fit.** OBJ and glTF use `RWObj_CafWriter` and `RWGltf_CafWriter`
   on the XCAF document the importer already built. They keep names, colors and (glTF) the
   assembly tree, and they write only the chosen parts through their label filter. STL is a short
   binary writer of our own over the face triangulations: `StlAPI_Writer` takes one shape and
   cannot convert units, and an STL has nothing to keep but triangles.
2. **Which parts.** "Visible parts" (the default) or "Selected parts" (offered when something is
   selected). Parts hidden in the viewer or in the file are left out. Section planes do not cut the
   export: parts are written whole.
3. **Mesh quality presets.** Every part is already meshed at the viewer's quality when the file
   opens, so:
   - **As displayed** (default): writes those meshes. Fast, no extra memory.
   - **Fine**: a quarter of the display's chordal tolerance, 10° angle.
   - **Extra fine**: a tenth of the display's chordal tolerance, 5° angle.
   - **Custom**: a chordal tolerance in the export unit and an angle.

   The display tolerance is relative to each part's size (the viewer's rule), so the presets are
   too. Custom is absolute, for users who need a known tolerance.
4. **Finer quality works on a private copy.** The open model's meshes are what the viewer, picking
   and the open-file cache use, so export never re-meshes them. For Fine, Extra fine and Custom,
   the export job copies the document without its triangulations (the cache's BinXCAF writer to
   memory, then read back), meshes the copy at the chosen quality and writes from it. The copy is
   freed when the export ends. While it runs, memory grows by about the model's size again plus
   the finer mesh; the dialog says so for finer presets.
5. **STL per part means one file per unique part.** A bolt used 20 times gives one file, in the
   part's own coordinates, named after the product (`<name>.stl`; repeated names get `-2`, `-3`;
   characters that file systems refuse become `_`). The user picks a folder. A merged STL has
   every chosen instance in place, in world coordinates.
6. **Units and axes.**
   - STL and OBJ: the unit is millimetres by default, or centimetres, metres or inches.
   - STL: the model's own axes (Z up), as in the STEP file, because slicers for 3D printing expect
     Z up.
   - OBJ: Y up by default, the convention most mesh tools (Blender, game engines) expect, or Z up
     as in the STEP file. Y up turns the model so its +Z becomes +Y.
   - glTF: metres and Y up, as the glTF spec requires (OCCT's coordinate converter). There is no
     unit choice.
7. **Instancing.** glTF shares one mesh between the instances of a part, so a repeated part costs
   little. OBJ and merged STL have no instancing and repeat the triangles.
8. **Colors.** Part and face colors from the file, as shown in the viewer, with transparency for
   glTF. glTF merges each part's faces into one primitive per color (`SetMergeFaces`), so web
   viewers draw it fast. STL has no color.
9. **Background job, modal progress.** An `ExportJob` in `src/app` runs the export on a worker
   thread, like `ImportJob`, behind a modal progress dialog with Cancel. Because the dialog is
   modal, the model cannot be closed or replaced mid-export. Cancel deletes the partial files. On
   success the status bar shows the file name and size. Errors (disk full, no permission, a writer
   failure) appear in a message box and leave no partial files.
10. **The dialog remembers its choices** (format, parts, quality, unit, STL mode, OBJ up axis) in `QSettings`
    under `exportGeometry/`. The file dialog starts in the folder last exported to, with the
    model's name.
11. **Code layout.**
    - `src/core/GeometryExport.{h,cpp}` (OCCT only): `exportGeometry(index, parts, options,
      progress, cancel)` does the copy, the meshing and the writing, and returns a result with
      the files written or an error.
    - `src/app/ExportGeometryDialog.{h,cpp}`: the options.
    - `src/app/ExportJob.{h,cpp}`: the worker thread.
    - `MainWindow`: the menu item and the wiring.
    - Mesh writing on the copy takes the importer's lock, because OCCT's XCAF application is
      shared.
12. **Build.** Link OCCT's mesh I/O toolkits: `TKRWMesh` and `TKSTL` before OCCT 7.8;
    `TKRWMesh`, `TKDESTL`, `TKDEOBJ` and `TKDEGLTF` from 7.8. glTF writing needs OCCT built with
    RapidJSON: add the `rapidjson` feature to `opencascade` in `vcpkg.json` and RapidJSON (MIT)
    to `THIRD_PARTY_NOTICES.md`. Ubuntu's OCCT 7.6 already has it. An OCCT built without it fails
    the glTF export with a clear message.

## Out of scope

- Writing STEP, IGES or other B-rep formats (PLAN §1 non-goal).
- PMI, edges, section caps and measurement witnesses in the export.
- ASCII STL, 3MF, PLY, FBX, USD and Draco-compressed glTF.
- Choosing the up axis for STL, or any up axis but Y or Z for OBJ.
- A command-line export (no CLI yet).

## Tests

- `core.*` (GoogleTest, OCCT only), `GeometryExportTest`, on small models built in the test:
  - merged STL: triangle count, and the bounding box in each unit;
  - per-part STL: one file per unique part, file naming with repeated and unsafe names, parts in
    their own coordinates;
  - OBJ: read back with `RWObj_CafReader`, names and colors kept, Y up and Z up;
  - glTF and GLB: read back with `RWGltf_CafReader`, assembly structure, instances sharing one
    mesh, metres;
  - only the chosen parts are written;
  - finer presets give more triangles and leave the source document's meshes untouched;
  - cancel removes partial files, and an unwritable path returns an error.
- `app.pmi_panel` (offscreen): `ExportGeometryDialogTest`: which options are enabled for each
  format, Selected parts only when there is a selection, settings round trip.
- `app.main_window`: the menu item's enabled state, and an STL export of the test model through
  the job that writes a non-empty file.
