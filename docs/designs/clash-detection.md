# Clash and interference detection

Status: APPROVED (2026-11-23)

## What

**Measure > Check Clashes...** finds part instances that overlap, touch, or come closer than a
clearance, among the selected parts (every visible part when nothing is selected). A **Clashes**
panel lists each pair with its kind and value: the overlap volume for an interference, the gap for
a clearance problem. Selecting a row selects both parts and draws the overlap (or the gap as a
line between the closest points) in red; double-clicking zooms to the pair. The check runs in the
background with progress and Cancel.

## Why

Checking an assembly for interference is a common design-review task (TODOS v2), and other
viewers offer it. The geometry check (v1.1) covers each part alone; this covers parts against each
other.

## Decisions

1. **Pairs of instances, in world space.** Two instances of the same part are checked against
   each other (a pattern of bolts can collide). Hidden parts are skipped, like mass properties.
2. **Options (a small dialog, remembered).**
   - **Interference** is always reported.
   - **Touching** (default off): faces in contact with no overlap. Seated parts touch everywhere
     in a real assembly, so it is opt-in.
   - **Clearance below** _x_ mm (default off, 1 mm when turned on): pairs closer than _x_ that
     neither overlap nor touch.
3. **Broad phase.** One exact bounding box per part definition (`BRepBndLib::AddOptimal`, no
   triangulation), moved to each instance; the boxes are enlarged by half the clearance plus
   0.01 mm so the pass never drops a real pair; then a sort-and-sweep on X finds the box pairs
   that overlap. Only those pairs go on to the exact check, which keeps large assemblies usable.
4. **Exact check per candidate pair.**
   - `BRepExtrema_DistShapeShape` gives the minimum distance and the closest points.
   - Distance above the clearance (or above 0.001 mm with no clearance): no clash.
   - Distance up to 0.001 mm (the touch tolerance): the parts touch or overlap. When both are
     solids, `BRepAlgoAPI_Common` gives the shared volume; more than 0.001 mm³ is an
     **interference** (the common solid is the highlight), less is **touching**. Sheet or wire
     parts can only touch.
   - Otherwise it is a **clearance** problem with the gap and the line between the closest points.
   - A pair whose check throws or whose Boolean fails is counted as "could not be checked" and
     listed as such, so a failure is never silent.
5. **Results.** Sorted by kind (interference, then touching, then clearance) and then by value
   (largest overlap, smallest gap first). The summary reads "3 interferences, 1 touching pair
   among 45 parts (120 pairs checked)." or "No clashes among 45 parts (120 pairs checked).". At
   most 2,000 rows are listed; the summary counts every clash.
6. **Units.** Volumes and gaps use the Measure menu's length unit and precision.
7. **Background job.** A `ClashJob` runs the check on a worker thread, like the geometry check.
   Cancel stops between pairs. Opening another model or closing this one cancels it and clears
   the panel. One check (geometry or clash) of each kind at a time.
8. **Code layout.**
   - `core/Clash.{h,cpp}` (no Qt): `findClashes(index, parts, options, progress, cancel)`,
     `candidatePairs(boxes)` (the broad phase, exposed for tests) and `checkPair(a, b, options)`.
   - `render`: reuses `SceneManager::showIssue` for the red overlap or gap line, and `select` for
     the pair.
   - `app`: `ClashDialog` (options), `ClashJob`, `ClashPanel` (dock) and the wiring in
     `MainWindow`.

## Out of scope

- Ignoring pairs by rule (same sub-assembly, fasteners, "known" contacts) and saving accepted
  clashes: wait for user feedback.
- Mesh-only checking (approximate but faster): only if the exact check is too slow on real
  assemblies.
- Checking parts against a section plane or against hidden parts.

## Tests

- `core.Clash.*`:
  - two overlapping boxes give an interference with the right volume;
  - two boxes sharing a face touch (only reported with Touching on);
  - two boxes 0.5 mm apart are a clearance problem with clearance 1 mm, and nothing without it;
  - far-apart boxes are not candidate pairs (broad phase);
  - the fixture: the two visible pins stand on the plate, so with Touching on there are exactly
    two touching pairs, and none without it; cancel stops.
- `app.pmi_panel` (offscreen): `ClashPanel` summary, rows and the chosen row; `ClashDialog`
  defaults and remembered choices.
- `app.main_window`: the fixture, Touching on, lists two pairs, and selecting a row selects both
  parts.
