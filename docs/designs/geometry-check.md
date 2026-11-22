# Geometry validity check

Status: APPROVED (2026-11-22)

## What

**Measure > Check Geometry** checks the selected parts, or every visible part when nothing is
selected, and lists what is wrong in a **Geometry Check** panel. Each problem has its part, its
kind and a detail. Selecting a row highlights the faulty face, edge or vertex in red in the 3D
view, and double-clicking zooms to it. The check runs in the background with a progress bar and
Cancel.

## Why

Bad geometry from the exporting CAD system is why a measurement, a mass property or a section
cap can come out wrong, and today the user has no way to tell (TODOS v1.1).

## Decisions

1. **Three kinds of problem.**
   - **Invalid geometry:** OCCT's full shape check (`BRepCheck_Analyzer`, every sub-shape and
     every status, such as "self-intersecting wire", "invalid curve on surface" or "free edge").
     The detail is OCCT's status name in plain words.
   - **Open shell:** a shell of a solid with free edges (`ShapeAnalysis_Shell`): the solid has a
     gap, so its volume and mass are wrong. The highlight is the free edges. Sheet bodies
     (shells without a solid) are open on purpose and are not reported.
   - **Tiny edge:** an edge shorter than 0.001 mm, not counting degenerate edges (the pole of a
     sphere, for example). Such edges break meshing and picking.
2. **Per part definition.** The check runs once per unique part, like the parts list; the row
   names the part and how many instances share the problem. The highlight uses the instance
   that was checked (the selected one, or the first visible one).
3. **At most 200 rows per part.** A badly broken part can have thousands of statuses; the panel
   lists the first 200 and says how many more there were. The summary counts them all.
4. **Summary line.** "No problems found in 12 parts." or "7 problems in 2 of 12 parts.", and
   the same in the status bar.
5. **Background job.** A `GeometryCheckJob` runs the check on a worker thread, like the
   geometry export. The check only reads shapes. Cancel stops between parts. Opening another
   model or closing this one cancels it and clears the panel.
6. **Code layout.**
   - `core/GeometryCheck.{h,cpp}` (no Qt): `checkGeometry(index, parts, progress, cancel)`
     returns the issues, and `statusText(status)` gives the plain-words name.
   - `render`: `SceneManager::showIssue(worldShape)` / `clearIssue()` draw the red highlight on
     top.
   - `app`: `GeometryCheckPanel` (dock), `GeometryCheckJob`, and the menu item and wiring in
     `MainWindow`.

## Out of scope

- Repairing geometry (OCCT's ShapeFix): this is a viewer.
- Checks between parts (interference is the clash detection item in v2).
- Tolerance analysis and a configurable tiny-edge length.

## Tests

- `core.*`:
  - a box has no problems;
  - a box missing a face, sewn into a solid, is an open shell with 4 free edges;
  - a 0.0005 mm thin box has tiny edges;
  - a bow-tie face is invalid with a self-intersecting wire;
  - the per-part row cap;
  - cancel;
  - `statusText`.
- `app.pmi_panel` (offscreen): `GeometryCheckPanel` shows the summary and rows, and selecting a
  row emits the issue.
- `render.viewport_smoke`: `showIssue` displays the highlight and `clearIssue` removes it.
- `app.main_window`: the fixture checks clean, and the menu item runs the job.
