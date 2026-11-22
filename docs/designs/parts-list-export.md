# Parts-list (BOM) export

Status: APPROVED (2026-11-22)

## What

**File > Export Parts List...** writes a CSV bill of materials: one row per unique part, with
its quantity in the model, names, where it is used, the file's attributes (part number, ...),
and optionally its material, volume, mass and size. A dialog picks the parts (all, or the
visible ones) and the optional columns; the file dialog then picks where to save.

## Why

A parts list is one of the most common things to pull out of a STEP assembly, and other viewers
offer it (TODOS v1.1).

## Decisions

1. **One row per part definition.** Instances group by their definition (the same key materials
   use, `AssemblyIndex::prototypeId`). Rows appear in the order a part first appears in the
   tree, numbered 1, 2, 3.
2. **Columns.**

   | Column | Content |
   |---|---|
   | Item | 1, 2, 3, ... |
   | Quantity | instances of the part among the chosen parts |
   | Part | the product name |
   | Instance names | the instances' own names when they differ from the product's, distinct, `; ` separated |
   | Used in | the distinct assemblies the instances sit in (display paths), `; ` separated; empty for a part at the top level |
   | *attributes* | one column per user-defined attribute key any part has (Part number, Material, Revision, ...), in order of first appearance |
   | Material used, Density (kg/m³) | the material the mass uses (the user's, else the file's), when the mass columns are on; named apart from a file attribute called "Material" |
   | Volume each (unit³) | exact B-rep volume of one instance |
   | Mass each, Mass total (mass unit) | volume × density, and × quantity; empty without a material |
   | Size X, Y, Z (unit) | the part's own axis-aligned bounding box |

   Lengths, volumes and masses use the Measure menu's length unit, mass unit and precision, so
   the CSV matches what the app shows. Units are in the headers ("Volume each (mm³)").
3. **A total row** after the parts: the total quantity, and the total mass when every row has a
   mass (an incomplete total would look right and be wrong, as in mass properties).
4. **Which parts.** All part instances of the model, or only the visible ones. Parts hidden in
   the file count as hidden.
5. **CSV format.** UTF-8 with a byte-order mark (spreadsheets then show "mm³" correctly), comma
   separated, quoted only when needed, like the Measurements export. **Formula injection:** text
   that comes from the file (names, paths, attribute values) and starts with `=`, `+`, `-`, `@`,
   a tab or a carriage return gets a leading `'`, so a spreadsheet shows it as text instead of
   running it. Numbers are written as numbers.
6. **Speed.** Volumes and sizes are computed once per part definition, not per instance. Big
   models show a progress dialog with Cancel (computed on the GUI thread between progress
   updates; parts lists are much quicker than meshing or export). Without the geometry columns
   nothing is computed.
7. **Code layout.**
   - `core/PartsList.{h,cpp}` (no Qt): `buildPartsList(index, parts, options, materialOf,
     progress, cancel)` and `toCsv(list, options)`.
   - `app/PartsListDialog` for the options. `MainWindow` has the menu item and
     `exportPartsListTo(path, options, visibleOnly)`.

## Out of scope

- Other formats (XLSX, PDF); a printable report.
- An indented, multi-level BOM (one row per assembly level).
- Editing quantities or adding parts that are not in the model.

## Tests

- `core.*`:
  - the fixture's list: 2 rows (plate ×1, pin ×4), names, used-in paths, attributes as
    columns, volume and size per part, a mass from a material, and the total row;
  - visible-only input;
  - CSV quoting and formula-injection guarding;
  - cancel.
- `app.pmi_panel` (offscreen): `PartsListDialog` options and their settings.
- `app.main_window`: an export of the fixture writes the expected header and rows.
