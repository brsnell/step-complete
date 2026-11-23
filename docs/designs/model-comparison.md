# Model comparison

Status: APPROVED (2026-11-23)

## What

**File > Compare With...** opens another revision of the open model and compares the two, part
by part. The open model (the **current** revision) is colored by what changed:

- **Changed** parts by their deviation from the other revision: green within the tolerance, then
  towards red where material was added and towards blue where it was taken away, with a legend;
- **Moved** parts (the same geometry in another place) in purple;
- **Added** parts (only in the current revision) in orange;
- **Unchanged** parts in light grey;
- **Removed** parts (only in the other revision) as red see-through ghosts where they were.

The **Comparison** panel lists every part that is not unchanged, with its change and largest
deviation, and a summary ("3 changed, 1 moved, 1 added, 2 removed, 40 unchanged"). Choosing a
row selects the part (or shows the removed ghost), double-clicking zooms to it. **End
Comparison** brings back the normal display.

## Why

Checking what changed between two revisions is a frequent review task (TODOS v1.2, PLAN §1), and
today it means opening both files and looking back and forth.

## Decisions

1. **No alignment step: the revisions share their coordinates.** Two exports of one model from
   one CAD system place it the same way, which is what this compares. Best-fit alignment of
   unrelated files is out of scope; a moved part is reported as Moved, a moved whole model as
   all Moved or Changed.
2. **The other revision is not a second document in the scene.** It is imported on the
   comparison's worker thread and kept as data (its index), never displayed except as the ghosts
   of removed parts. The scene keeps one model, the tree and Properties stay the current
   revision's, and nothing in `SceneManager` or `MainWindow` has to hold two models.
3. **Pairing parts, in stages.**
   1. By path below the root ("Pin pair / Pin Ø6"; the root's own name is left out, since it is
      often the file name). Repeated paths pair in tree order.
   2. What is left, by the same shape (volume, area, principal moments), nearest in place first:
      a renamed or re-exported part still pairs.
   3. What is still left, by part name ignoring case and an instance number ("nut_2" and
      "NUT"): a part that was renamed by its exporter and changed.
   Parts without any geometry (empty compounds some exporters write) are left out. A part with
   no partner is Added (current only) or Removed (other only).
4. **Unchanged, Moved or Changed: by exact properties first.** For each pair, volume, surface
   area, centre of mass, principal moments of inertia and bounding box are compared (relative
   1e-6, or the tolerance for positions). Equal in world coordinates: **Unchanged**. Equal in the
   parts' own coordinates but not in the world: **Moved**. Otherwise **Changed**. Exact
   properties do not suffer from the mesh's chordal error, so re-exported identical parts never
   show as changed.
5. **Deviation of a changed part from the meshes.** For each point of the current part's mesh,
   the signed distance to the other part's mesh (positive outside it: material added); and the
   largest distance the other way round, so material that was taken away is not missed. A
   bounding-volume hierarchy over the other mesh keeps each point near log(n), and points run in
   parallel. The meshes are refined like the wall thickness map (`core/PartMesh`), so a large
   flat face gets points inside it: a box face alone has only its corners, which can lie on the
   other revision's surface while the face between them moved. The values are accurate to the display mesh (a fraction of a millimetre), which
   the legend says; the classification itself does not depend on them.
6. **Tolerance.** 0.01 mm by default, set in the panel; deviations within it are green. Changing
   it recomputes.
7. **Display.** Like the wall thickness view: each current part is swapped for a colored
   presentation through `SceneManager::setPartOverride` (so selection, hiding and sections keep
   working), with a legend from minus to plus the largest deviation. The colored-mesh
   presentation is shared with the wall thickness view (`ColorMapPresentation`); in both, a
   selected part shows its bounding box rather than being painted in one color, which would hide
   the colors. The comparison and the thickness view exclude each other. Measuring needs the
   parts' own faces, so starting a measurement ends the comparison, like the thickness view. The
   removed parts' ghosts are not cut by section planes.
8. **Background job.** A `ComparisonJob` imports the other file and compares on a worker thread
   with progress and Cancel. Opening or closing a model ends the comparison.
9. **Code layout.**
   - `core/PartMesh` (the merged, outward-wound mesh of a part, shared with wall thickness),
     `core/MeshDistance` (closest point on a mesh), `core/Comparison` (pairing, classification,
     deviation).
   - `render/ColorMapPresentation` (shared), `render/ComparisonScene`.
   - `app/ComparisonJob`, `app/ComparisonPanel`, and the wiring in `MainWindow`.

## Out of scope

- Aligning models that do not share coordinates (best fit, three-point).
- Showing the other revision side by side, or both overlaid in full.
- Comparing PMI, attributes or colors; a comparison report file.

## Tests

- `core.Comparison.*`:
  - identical revisions (the fixture against itself, through a STEP round trip): every part
    Unchanged;
  - a pin moved: Moved; the plate made thicker: Changed with a deviation of the added
    thickness; a part only in one revision: Added or Removed;
  - pairing by path with repeated names, a renamed part pairing by shape, "Nut_2" and "NUT"
    pairing by name; a part turned about its own centre is not Unchanged; a thinner plate
    reads -2 mm across its top; cancel.
- `core.MeshDistance.*`: distances and signs to a box mesh.
- `render.viewport_smoke`: the comparison view colors, ghosts and restores.
- `app.pmi_panel`: the panel's summary, rows and tolerance.
- `app.main_window`: Compare With a changed revision lists the change; End Comparison restores.
