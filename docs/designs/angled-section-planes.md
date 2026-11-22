# Section planes at any angle

Status: APPROVED (2026-11-22)

## What

Each of the three section planes can lie at any angle, not only across X, Y or Z:

- **Pick Face**: a button on each plane row. The next click on a flat face puts the plane on
  that face.
- **Custom normal**: choosing **Custom** in the plane's axis list shows X, Y and Z fields for the
  normal (the direction the plane faces).

Offset, slider, handle dragging, Flip, Cap and Hatch work the same for every direction.

## Why

Real parts are rarely cut usefully along the world axes alone: a flange at 30°, a hole drilled
at an angle, a rib along a diagonal. Other viewers offer both a picked plane and a typed one
(TODOS v1.1).

## Decisions

1. **The render side already supports it.** `SectionPlane` takes any `gp_Dir` normal, and the
   clip equation, the offset range (the model's box projected on the normal), the handle and
   its drag all work for any direction. Only the Section panel limits the choice today, and it
   already shows **Custom** when a plane is not axis-aligned. The work is in the panel and in
   picking.
2. **A picked face sets the plane exactly on it.** The normal is the face's outward normal in
   world coordinates, with the part's placement and the face's orientation applied. The offset
   puts the plane through the face, so at first nothing of that part is cut away. Dragging the
   handle against its arrow (or typing a smaller offset) then cuts into the part from that
   face. Flip cuts the other side.
3. **Flat faces only.** A click on a curved face, an edge or empty space keeps the pick active
   and says "Pick a flat face" in the status bar. A cylinder's axis or a face's tangent plane
   is out of scope.
4. **One click, then back to normal.** Pick Face shows "Click a flat face for plane N (Esc
   cancels)" in the status bar and makes faces pickable, as the measure tool does. The button
   sits beside the plane's checkbox, so it works on a plane that is off; the first flat face
   turns the plane on and ends the pick, and a cancelled pick leaves the plane as it was. **Esc**, pressing Pick Face again, or starting a
   measurement cancels it. Picking stops any active measurement, so only one tool owns clicks.
   The tree and 3D selection are left as they were.
5. **Custom normal fields.** Three spin boxes (X, Y, Z, three decimals) appear under the axis
   list while it shows Custom. The normal is the vector they make, normalized; an all-zero
   vector is refused (the fields revert). Choosing X, Y or Z again hides them. A picked face
   also shows its normal there.
6. **The offset follows the new direction.** After any change of direction the offset is pulled
   into the new range, as choosing a different axis does today.
7. **Code layout.**
   - `render::SectionTool`: `pickFace(index)` / `cancelPick()` / `isPicking()`. It sets face
     selection modes through `SceneManager` (as `MeasureTool` does), reads the picked face's
     world shape with `SceneManager::pickedEntity`, computes the plane, and emits
     `facePicked(index)` or `pickRejected()`.
   - `core`: `planeOfFace(face)` returns the outward plane of a planar face, or nothing. No Qt,
     unit tested.
   - `app::SectionPanel`: the Pick Face button and the Custom fields. `SectionModel` gains
     `pickFace` / `cancelPick` so the panel stays testable without OpenGL.
   - `MainWindow`: status messages, Esc, and stopping measurement while a pick runs.

## Out of scope

- Planes through three picked points, along an edge, or tangent to a curved face.
- Rotating a plane by dragging in 3D (only the offset drags).
- Saving planes with the file; user saved views (a later v1.1 item) will cover that.

## Tests

- `core.*`: `planeOfFace` for a box face (with and without a placement and a reversed face) and
  a rotated planar face; nothing for a cylinder's side.
- `app.pmi_panel` (offscreen), `SectionPanelTest`: Custom shows the X/Y/Z fields and sets a
  normalized normal; a zero vector is refused; X/Y/Z hides the fields; Pick Face calls the model
  and a picked plane shows Custom with its normal; the offset is clamped after a direction
  change.
- `render.viewport_smoke`, `SectionToolTest`: picking a face of the fixture puts the plane on it
  (normal and offset); a curved face is rejected and the pick stays active; cancel restores the
  selection modes.
