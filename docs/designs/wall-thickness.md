# Wall thickness analysis

Status: APPROVED (2026-11-23)

## What

**Measure > Wall Thickness** colors the selected parts (every visible part when nothing is
selected) by their local wall thickness: red where the wall is at or below the **thin limit**,
through yellow and green, to blue at or above the **thick limit**. A legend in the 3D view shows
the scale. Hovering over a colored part shows the thickness under the cursor. The **Wall
Thickness** panel gives the range found and the two limits, which can be changed. Turning the
menu item off brings back the normal display.

## Why

Checking castings, moldings and printed parts for walls that are too thin (or too thick) is a
common review task (TODOS v2, PLAN §1).

## Decisions

1. **Ray casting on the mesh, not exact B-rep thickness.** At each mesh point a ray goes into the
   part along the inverted surface normal. The thickness is the distance to the first triangle
   it leaves the part through: a triangle facing away from the ray. Exact B-rep ray casting
   would be precise but costs seconds per point on large parts. The mesh is already in memory
   and the error is the display deflection (a fraction of a millimetre at default quality),
   which is small next to the wall thickness being checked. This is the same "ray method" that
   CAD tools use.
2. **A finer mesh for the map.** The display mesh can put two triangles on a large flat face,
   which would give thickness only at its corners. Triangles with an edge longer than 1/150 of
   the part's size are split into four, up to four times, and at most 300,000 points per part.
   Points on split triangles lie on the mesh, not exactly on curved surfaces; the error is again
   within the deflection.
3. **Acceleration.** A bounding-volume hierarchy over each part's triangles (median split, built
   once per part definition) keeps each ray at about log(n) triangle tests. Points are processed
   in parallel. A point with no exit (an open shell or a sheet) has no value and is drawn grey.
4. **Per part definition.** Thickness does not depend on where an instance sits, so it is
   computed once per definition (in its own coordinates) and every instance shares the map.
5. **Display.** The scene can swap a part's normal presentation for another one while keeping
   its visibility, selection and section planes (`SceneManager::setPartOverride`). The thickness
   presentation draws the refined mesh with per-vertex colors and is selectable as a whole part,
   so selecting, hiding, isolating and section views keep working. Hovering does not highlight a
   colored part (it would paint over the colors); selecting still does. Measuring needs sub-shape
   picking, which the colored parts do not offer, so starting a measurement turns the view off.
   The legend is OCCT's `AIS_ColorScale` in the lower-left corner. Display styles (wireframe,
   hidden line) do not apply to the thickness view.
6. **Readout.** While the view is on, hovering a colored part shows "2.35 mm" next to the cursor
   (the overlay marker the measure tool uses). It casts one ray at the hovered point: the ray
   first finds the hovered triangle, then goes inward from it.
7. **Limits.** By default the limits are the smallest and largest thickness found, leaving out
   the most extreme 0.1% of points at each end: single points at sharp corners of curved faces
   can read far off (0.02 mm on a NIST model whose walls are 9 mm and more), and one of them
   would set the whole scale. Grey points (no exit) are counted and reported; on the NIST models
   they come from gaps in the file's own mesh. The panel's **Thin limit** and **Thick limit**
   recolor at once without recomputing. Values use the Measure menu's length unit.
8. **Background job.** A `ThicknessJob` computes on a worker thread with progress and Cancel.
   Only the triangulations are read. Opening another model or closing this one turns the view
   off.
9. **Code layout.**
   - `core/WallThickness.{h,cpp}` (no Qt): `computeThickness(shape)` gives the refined mesh and
     the per-point values for one part; `thicknessAt(map, point, direction)` gives the readout;
     `analyseThickness(index, parts, progress, cancel)` runs every definition.
   - `render`: `ThicknessPresentation` (colored mesh) and `ThicknessScene` (overrides, legend,
     limits, readout); `SceneManager::setPartOverride`.
   - `app`: `ThicknessJob`, `ThicknessPanel` (dock) and the wiring in `MainWindow`.

## Out of scope

- The sphere method (largest inscribed sphere), which reports thin regions differently from the
  ray method at corners and ribs.
- Draft-angle and undercut analysis.
- Saving the limits per file or exporting a thickness report.

## Tests

- `core.WallThickness.*`:
  - a 10 × 10 × 2 mm plate reads 2 mm on its large faces and 10 mm across its length;
  - a 20 mm sphere reads its diameter;
  - a tube (outer radius 10, inner 8) reads 2 mm on its walls;
  - refinement adds points on a big plate and respects the point cap;
  - `thicknessAt` on a placed instance;
  - a sheet (one face) has no values; a shape with no mesh gives no map;
  - the fixture, and cancel.
- `app.pmi_panel` (offscreen): `ThicknessPanel` summary and limits.
- `render.viewport_smoke`: overrides keep visibility and selection; the thickness view draws red
  and blue where expected.
- `app.main_window`: the menu item computes the fixture, shows the panel and turns off again.
