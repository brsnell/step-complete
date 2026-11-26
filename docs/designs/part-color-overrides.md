# Part color overrides

Status: APPROVED (2026-11-26)

## What

**View > Part Color...** (`C`) gives the selected parts a color of the user's choosing: a color
dialog opens on the first selected part's current color, and the chosen color replaces the
file's colors on every selected part instance (an assembly applies it to every part below it).
**View > Reset Part Color** puts the file's colors back on the selected parts. Both are on the
3D view's and the assembly tree's context menu too.

The colors are kept with the model's own data (the per-model store that saved views, materials
and markup use), never in the STEP file, and come back when the file is opened again. Image
export shows them; geometry export writes the file's colors as before.

## Why

Seen in other viewers and listed as a candidate in `docs/PLAN.md`: telling parts apart in a
review ("the new bracket in orange"), when the file gives everything the same gray, or colors
that hide the part being discussed.

## Decisions

1. **Per part instance.** A color is set on instances, not on the part definition: coloring one
   of four identical bolts colors that bolt. Selecting an assembly colors every part below it,
   as Hide and Transparent do. Stored by the instance's stable path id
   (`AssemblyIndex::pathId`), so it finds the same instance when the file is opened again; an id
   the reopened file does not have is ignored (and kept, in case the file comes back).
2. **The whole part takes the color.** The override replaces the file's per-face colors as well
   as the part's own: a part is one color. Edges keep their dark outline. The part's
   transparency is kept. Hidden-line keeps its white faces (it is a drawing style, not a
   color one); Shaded, Shaded with Edges and Wireframe show the color.
3. **How it is drawn.** An instance with its own color is detached from the shared prototype
   presentation (as Transparent does, since connected instances share their prototype's
   aspects), and its presentation's style rebuild drops the file's per-face styles and sets the
   override, the same place it already puts transparency and hidden line back. Reset gives the
   presentation its file styles again.
4. **Other views.** Wall thickness and comparison draw their own colors over the parts while
   they are on; the override is back when they end. The exploded view, section planes,
   selection highlighting, hiding and isolating work as before.
5. **Stored form.** `"partColors": { "<pathId>": "#rrggbb", ... }` in the model's data,
   written at once on each change, like materials.
6. **Code layout.**
   - `render`: `SceneManager::setPartColor(nodes, std::optional<Quantity_Color>)`,
     `partColor(node)`; `PartPresentation` gains the override.
   - `app`: `MainWindow::setPartColor(nodes, std::optional<QColor>)`, `partColorOf(node)`, the
     two View menu actions and context-menu entries, loading and saving `partColors`.

## Out of scope

- Colors per face, per definition, or with their own opacity.
- Writing the overrides into exported geometry or the STEP file.
- Saving colors in the user's saved views (a view restores camera, visibility and sections).

## Tests

- `render.viewport_smoke`: a colored instance draws in its color and its twin instance keeps the
  file's color; a part with per-face file colors draws all faces in the override; Reset brings
  the file's colors back; the color survives a display style change and transparency.
- `app.main_window`: Part Color applies to the selected parts (an assembly to all below), is
  written to the model's data and comes back after reopening the file; Reset clears it.
