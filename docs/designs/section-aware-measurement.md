# Measurements that follow section views

Status: APPROVED (2026-11-27)

## What

With a section view on, measuring works on what the user sees:

- **A click on a cut face measures the cut face.** With **Cap** on, a click on the filled cut face
  of a part (its cap) picks that flat face, the part's cross-section in the plane, as a face pick
  in every mode: its area in Entity Info, a distance or an angle to it, and Point to Point snaps
  to its corners and edge midpoints. The report names it "Section face on <part>". Today the
  click goes through the cap and picks the part's far wall behind it, which the user cannot see.
- **The plane handles let measuring clicks through.** While a tool picks faces, edges or
  vertices (measuring, a plane's face pick, placing a note), the translucent handles are not
  pickable, so a click on a part behind a handle reaches the part. Today the handle takes the
  click and the measure tool drops it without a word, so nothing behind a handle can be
  measured. The handles are pickable and draggable again when the tool ends; the Section panel's
  slider moves a plane in the meantime.
- **Picks on the cut-away side** are already refused: OCCT's selector skips clipped geometry.
  This design keeps that and tests it.

## Why

Left open in M2 ("Results that follow section views", `TODOS.md` Deferred). Sections are how users
reach inner faces (a bore, a wall's inside), so measuring them is the common next step, and today
it either measures a hidden face or does nothing.

## Decisions

1. **Which cap was clicked.** The ray from the eye through the hit point enters the kept side of
   each plane it crosses from the cut side; the last such crossing before the hit is where the
   visible part of the model starts. If that plane is capped and its crossing point is inside one
   of the hit part's solids, the user clicked that plane's cap on that part; otherwise the hit
   stands (an uncapped section shows the part's inside, and that is what was hit). The ray's
   direction is the camera's (orthographic) or from the eye to the hit (perspective).
2. **The cap face.** The part's solids in common with the plane, trimmed by the other enabled
   planes (a rectangle on the plane, cut by the others' half-planes, then one boolean with the
   part), and of those faces the one under the click. Faces only: the cap's outline edges and
   corners are reached through Point to Point snapping, not as edge or vertex picks. The cap
   faces are kept per part and plane until the planes change, so hovering in Point to Point mode
   snaps on the cap without a boolean per mouse move.
3. **Feedback.** A pending cap pick is drawn as the cap face in the selection color on top of the
   model, since the part's own highlight lands on the hidden far wall. Point to Point's hover
   marker snaps on the cap. Hovering in the other modes shows no pre-highlight on a cap.
4. **Witnesses stay whole.** Measurement witnesses are drawn on top of the model and are not
   clipped, like PMI: a measurement taken before the section, or one that spans the plane, keeps
   its full dimension line. The Measurements panel's results do not change when planes move.
5. **Handles.** A selection filter on the context rejects the handles' owners while the scene's
   selection modes are sub-shape modes (any picking tool), and accepts them otherwise.
6. **Code layout.**
   - `core/SectionCap.{h,cpp}` (OCCT only): `CutPlane`, `capHit` (decision 1), `capFaces` and
     `faceAt` (decision 2), `snapOnCap` (Point to Point on a cap: corners, then edge endpoints,
     midpoints and centers, then the nearest edge point).
   - `core::Pick::sectionFace` names the entity "Section face" in the report.
   - `render`: `MeasureTool` resolves cap picks, caches cap faces and draws pending cap picks; a
     cap pick is kept apart from OCCT's selection (which holds the far wall), so clicking the cap
     again un-picks it and the far wall stays pickable. `SectionTool` installs the handle filter.
     `SceneManager::pickedPoint` gives the point where the selector met a given owner: with a
     handle filtered out, the first hit is the handle, not the part. Measuring and note
     placement use it.

## Out of scope

- A plane's Pick Face and placing a note on a cap still go through to the far wall (a plane on a
  cap is the same plane; a note on a cap would move with the plane). Recorded in `TODOS.md`.
- Edge and vertex picks on a cap's outline (use Point to Point).
- Clipping witnesses, or re-measuring results when a plane moves.
- Caps of open shells (OCCT caps closed volumes only), and parts made of closed shells without
  solids (their caps still pick the far wall).
- The uncapped clip plane a PMI saved view may set on the view: only the section planes count.
- Radius on a cap's circular outline (the cap is a flat face; pick the hole's wall instead).

## Tests

- `core.SectionCap` (GoogleTest): a ray through a capped plane into a box reports the cap and its
  point; an uncapped plane, a ray that misses the solid, and a hit before any plane report none;
  the last of two planes wins; the cap face is the box's cross-section with the right area,
  trimmed by a second plane; a part with two solids gives the face under the point.
- `render.viewport_smoke` (MeasureTool): with a capped plane through the base plate, a click on
  the cap measures the section face (its area, its name) and draws the pending highlight; with
  Cap off the click picks the far wall; a click through a handle reaches the part; a click on the
  cut-away side picks nothing. (SectionTool): Pick Face works through a handle; the handle still
  drags when no tool is picking.
