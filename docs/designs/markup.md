# Markup

Status: APPROVED (2026-11-23)

## What

A **Markup** menu for review notes and sketches pinned to the model:

- **Add Note** (`Ctrl+Shift+N`): click a face, type the note. The note's text sits in a yellow
  box next to the point that was clicked, joined to it by a leader with a dot at the point.
- **Sketch** (`Ctrl+Shift+K`, on/off): while it is on, dragging with the left button draws a
  magenta freehand stroke over the model. The wheel, the middle button and Shift- or Ctrl-left
  dragging still move the view; Esc or the menu item ends sketching.
- **Show Markup** (on by default) shows or hides every note and stroke.
- **Export Markup...** / **Import Markup...**: a `.scmarkup` file (JSON) to send to someone who
  has the same model. Import adds the file's notes and strokes to the model's own.
- **Delete All Markup...** (asks first).

The **Markup** panel lists the notes (their text) and strokes ("Sketch 3"). Choosing a row
emphasizes that item in the view; double-clicking a note edits its text, double-clicking a
stroke zooms to it; **Delete** (or the Delete key) removes the chosen rows.

Markup belongs to the model: it comes back whenever the same file is opened again. Image export
(File > Export Image) includes the markup that is shown.

## Why

Reviewers need to point at things and say what is wrong with them (TODOS v1.3, PLAN §1). Today
the only way is a screenshot edited in another program, which loses the 3D context.

## Decisions

1. **Never in the model file.** Markup is stored with the model's other user data in the
   per-file store (`ModelDataStore`, docs/designs/user-saved-views.md) under its own key,
   `"markup"`. The store is found by the file's content, so copies and renames keep their
   markup and a file re-exported in place keeps it too. A sidecar file beside the model was
   rejected for the same reasons as for saved views (read-only model folders, clutter); the
   explicit export file covers sharing.
2. **Anchored to a part, in the part's own coordinates.** A note stores the part instance's
   stable id (`AssemblyIndex::pathId`, the XCAF label chain) and the clicked point in that
   part's coordinates; a stroke stores the part it was started on and its points in that part's
   coordinates. If a re-exported revision moves the part, its markup moves with it. The face is
   not stored: exporters renumber faces, and the point alone places the note. A stroke started
   over empty space is stored in world coordinates with no part. Markup whose part no longer
   exists is listed in the panel as "(part missing)" and not drawn, so it is never lost
   silently.
3. **Hidden with its part.** A note or stroke on a hidden part is hidden too (isolating a part
   shows only its markup). Section planes do not cut markup, like PMI labels.
4. **Drawing.**
   - Notes are drawn in screen space at a constant pixel size (like PMI labels): the box is
     offset 24 × 24 px up and to the right of the point, holds every line of the text, and stays
     on top of the model. Text uses the bundled PMI font. The emphasized note gets an orange
     border.
   - Strokes are 3 px magenta polylines (red is a common part color), also on top of the model
     so they are never buried in the surface they were drawn on. The emphasized stroke is orange.
5. **Where a stroke lies.** A stroke is drawn on the plane facing the camera through the point
   of the model under the press (or through the view's centre when the press is over empty
   space). Each mouse move adds the point where the cursor's ray meets that plane; points closer
   than 2 px to the previous one are dropped. A stroke of fewer than two points is discarded.
   Seen from another angle a stroke is a flat curve in space, as in other CAD viewers.
6. **Placing a note** uses face picking (like the section planes' Pick Face): the clicked point
   on the face is the anchor, then a dialog asks for the text (several lines allowed). Empty
   text cancels. Esc cancels the pick. Picking a face needs the parts' own faces, so Add Note
   ends measuring, the wall thickness view and the comparison view, the same way measuring does.
   Sketching only needs the hovered point, so it works in every view; it stops measuring (both
   want the left button).
7. **Writing.** Each change (add, edit, delete, import) is written to the store at once, like
   saved views.
8. **The export file** is the same JSON as the store's `"markup"` value plus a format version
   and the model's file name (shown when importing a file made for another model: "made for
   bracket.step; import anyway?"). Part ids only match the same model, so markup for parts that
   do not exist is listed as missing, per decision 2.
9. **Code layout.**
   - `core/Markup.h` (no Qt): `MarkupNote`, `MarkupStroke`, `Markup` (with ids that stay stable
     for the panel) and the world-to-part placement helpers.
   - `render`: `MarkupNoteLabel` and `MarkupStrokePresentation` (AIS objects), `MarkupScene`
     (shows the markup, visibility by part, emphasis, note picking, sketching).
     `OcctViewport::setPressHandler` lets a handler take every plain left press (sketching),
     before navigation and the existing drag handler.
   - `app`: `MarkupJson` (JSON form), `MarkupPanel` (dock), and the wiring in `MainWindow`.

## Out of scope

- Moving a note's box, arrows, shapes (circles, boxes), colors and line widths of one's own.
- Replies, authors, dates and statuses (an issue tracker), and markup shared live with others.
- Undo beyond deleting an item.
- Notes inside the STEP file or exported as STEP PMI.

## Tests

- `core.Markup.*`: part-to-world placement round trip, ids, removal.
- `app.pmi_panel` (offscreen):
  - `MarkupJson` round trip (notes with several lines, strokes with and without a part),
    malformed entries skipped, the export file's version and model name;
  - `MarkupPanel`: rows, missing parts, edit, delete.
- `render.viewport_smoke`: a note and a stroke draw over the model (pixels of their colors),
  hiding the part hides them, Show Markup off hides everything, emphasis; a drag in sketch mode
  makes a stroke on the plane through the hovered point.
- `app.main_window`: add a note on the fixture, close and reopen the file and it is back;
  export and import into an empty model; Delete All.
