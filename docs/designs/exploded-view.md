# Exploded view

Status: APPROVED (2026-11-25; follow-ups for assembly PMI, unbounded geometry and messages approved 2026-11-26)

## What

**View > Exploded View** (`Shift+E`) moves an assembly's parts apart so the user can see how it
goes together. The **Exploded View** panel has a **Distance** slider (0 to 100 %) that moves
them live, and a **Collapse** button that puts them back. The PMI and the markup on a part move
with it, and so do selection, hiding, isolating, transparency and Fit.

The explode is a way of looking at the model, not a change to it: nothing is saved, export
writes the assembled model, and opening or closing a model starts collapsed. Export Image
captures what is on screen, exploded or not.

## Why

A common viewer feature for understanding assemblies (TODOS, Deferred: exploded view). It was
deferred in M4 because moving parts would detach their PMI and break measurement picks unless
every subsystem learned about the offsets. This design gives the PMI and the markup the offsets
and keeps the tools that measure or compute the assembled geometry out of the exploded view.

## Decisions

1. **Direction: by assembly level.** Each part moves away from the center of the assembly it
   belongs to, and each sub-assembly moves as a unit away from the center of its parent, so a
   sub-assembly stays together while it moves out and its own parts spread around it. The
   offset of a node at distance factor `f` is

   `offset(node) = offset(parent) + f * (center(node) - center(parent))`, `offset(root) = 0`,

   where `center` is the center of the world bounding box (of a part, or of all the parts below
   an assembly). It is linear in `f`, so the directions are computed once per model (the
   bounding box of each prototype once, moved to each instance) and the slider only scales
   them. A node centered on its parent's center does not move at that level (a shaft inside a
   housing). For a flat assembly this is the usual radial explode.
2. **Distance.** The slider runs from 0 to 100 %: `f = percent / 100`, so at 100 % every part is
   twice as far from its assembly's center as in the assembled model. It starts at 50 %, and
   keeps the last value while the app runs. 0 % is the assembled model, so it collapses; moving
   the slider up from there explodes, and exploding from the menu at 0 % starts at 50 %.
3. **Moving the parts.** Each part instance's presentation (the connected instance, or its
   detached presentation) gets the local transformation `translation(offset) * placement` in
   place of `placement`. Nothing is re-meshed or re-uploaded, so dragging the slider is smooth
   on large assemblies. The bounding box (Fit, Fit Selection) follows from the presentations.
4. **What follows the parts.**
   - **PMI:** every PMI presentation, arrow and label of a part instance gets that instance's
     offset. PMI defined on an assembly (or the top level) follows the part instance its target
     faces, edges or vertices are on, found by matching them to the parts below (shape and
     placement); PMI with targets on several parts is hidden while exploded, as it would point
     at parts that moved apart, and PMI with no target found stays with its assembly. The
     labels' anchors move and the labels are decluttered again. Labels and arrowheads are drawn
     in pixels around a point, so they move by moving that point (a local transformation would
     move them by pixels). Highlighting a PMI's target geometry draws each target where its part
     is.
   - **Markup:** notes and strokes on a part move with it; items in the world stay put.
5. **What does not, and is kept out.** These tools measure or compute on the assembled geometry,
   or draw something between two parts; in an exploded view their results would be in the
   wrong place or mean something else. One mode at a time, as with section picks and
   measurements already:
   - Exploding turns off (and names in the status bar) the measure mode, a section plane or note pick, sketching, the
     section view, the wall thickness view, the comparison view and the geometry check or
     clash highlight, and hides the measurement witnesses (the Measurements panel keeps them,
     and they come back on collapse).
   - Starting any of those while exploded (a section plane's face pick included), or applying a
     saved view from the file (its camera and clipping were made for the assembled model),
     collapses first.
   - Restoring one of the user's own saved views works exploded, unless it turns on a section
     plane (then it collapses, as the section view does).
6. **When it is available.** With a model that has at least two part instances; otherwise the
   action is disabled. Whether they move apart (offsets not all zero) is known once the layout
   is computed, when the user first explodes; if none moves, it says so once, stays assembled
   and disables the menu item and the slider for that model. The layout is kept for the model it
   was computed for and computed again for another one. A part is bounded by its finite faces,
   edges and vertices when it also holds unbounded geometry (an untrimmed reference plane), so
   such a part still moves apart. A single part (an STL file, which is one part) has
   nothing to explode. A mesh file with several parts (OBJ, glTF) explodes like any assembly:
   moving parts needs no exact geometry.
7. **Code layout.**
   - `core/ExplodeLayout.{h,cpp}` (OCCT only): the per-node directions at `f = 1`, and
     `offset(node, f)`.
   - `render`: `SceneManager::setPartOffsets`, `PmiScene::setPartOffsets`,
     `MarkupScene::setPartOffsets` (a vector of world translations by node id; empty
     collapses). `PmiLabel::setAnchorOffset` moves a label's anchor.
   - `app/ExplodePanel.{h,cpp}`: the dock (`ExplodeDock`) with the slider and Collapse.
     `MainWindow` owns the layout, the exclusions above and the action.

## Out of scope

- Moving single parts by hand (drag to place), explode steps or animation, and saving an
  explode with the model.
- Measuring in the exploded view.
- Explode lines (trails from a part to where it came from).

## Tests

- `core.ExplodeLayout` (GoogleTest): a flat assembly explodes radially; a sub-assembly moves
  as a unit and its parts spread around its center; a centered part stays; `f = 0` gives no
  offsets; a single part cannot explode.
- `render.viewport_smoke`: offsets move a part's presentation and its bounding box, and empty
  offsets put it back; PMI presentations, arrows and labels of the part move with it; a markup
  note on the part moves with it.
- `app.main_window`: the action is enabled for an assembly and disabled with no model;
  exploding moves the parts and shows the panel; the slider changes the distance; exploding
  turns off a measure mode and the section view and hides the witnesses; starting a
  measurement collapses; closing the model collapses.
