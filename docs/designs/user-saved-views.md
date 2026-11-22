# User saved views

Status: APPROVED (2026-11-22)

## What

**View > Saved Views**:
- **Save Current View...** stores the camera, which parts are shown and the three section planes
  under a name.
- Each saved view is listed below it; choosing one restores all three.
- **Manage Saved Views...** renames, deletes and reorders them.

Views belong to the model: they come back whenever the same file is opened again.

## Why

The only saved views today are the ones a STEP file's PMI declares, and most files have none.
Users want their own for repeat inspections and screenshots (TODOS v1.1).

## Decisions

1. **Per-file data lives in the app's data folder, not next to the model.** One JSON file per
   model, `<AppData>/models/<fingerprint>.json`, with a section per feature (`"views"` now;
   material densities and markup will add theirs). A sidecar file beside the STEP file was
   rejected as the default: model folders are often read-only (network shares, PDM checkouts,
   downloads), and it would clutter them. Sharing views as a file (export/import) can come later
   on top of the same JSON.
2. **The key is the file's content, not its path.** The fingerprint hashes the file's size and
   sampled blocks (the open-file cache's sampling), without the modification time or the app
   and OCCT versions that the cache key includes. Copying, moving or renaming the file keeps its
   views, and so does upgrading the app.
3. **An edited file keeps its views.** A store index maps each file path to the fingerprint last
   seen there. When the fingerprint has no data but the path is known (the file was re-exported
   in place), the old data is used and moved to the new fingerprint. Parts that no longer exist
   are skipped when a view is restored.
4. **What a view stores.**
   - Camera: eye, target, up direction, projection (orthographic or perspective), the
     orthographic scale and the perspective field of view. The window's aspect ratio is not
     stored: a view restored in a differently shaped window keeps its centre and scale.
   - Hidden parts, as stable ids: the chain of XCAF label entries from the root to the instance
     (the same ids OCCT's mesh writers use). The same file always imports to the same entries.
   - The three section planes (on/off, direction, offset, cap, hatch).

   Display style, transparency, PMI visibility and the selection are not stored.
5. **Restoring** sets the camera, shows every part and hides the stored ones (so parts the file
   hides come back only if they were shown when the view was saved), and sets the section
   planes, turning the section view on or off to match. The status bar names the view.
6. **Names.** Save asks for a name ("View 1", "View 2", ... by default). An existing name asks
   whether to replace that view. Names are trimmed; empty names are refused.
7. **Writing.** The JSON is written atomically (temporary file, then rename) after every save,
   rename, delete or reorder. A document that cannot be read is reported in the status bar and
   the views list starts empty; the next save sets it aside as `<name>.unreadable` instead of
   overwriting it.
8. **Shortcut.** Save Current View is `Ctrl+Shift+V`; each saved view gets a menu number (1-9).
9. **Code layout.**
   - `core`: `fileFingerprint(path)` next to `cacheKey`; `AssemblyIndex::pathId(node)` and
     `nodeForPathId(id)`.
   - `render`: `OcctViewport::cameraState()` / `setCameraState()` with a plain `CameraState`
     struct.
   - `app`:
     - `ModelDataStore` (per-file JSON documents, the fingerprint and path index);
     - `UserView` with its JSON form;
     - `SavedViewsDialog` (manage);
     - the menu and restore logic in `MainWindow`.

## Out of scope

- Exporting or importing views as a file, and views shared between different models.
- Thumbnails, and storing display style, transparency, PMI visibility or measurements.
- Undo for deleting a view.

## Tests

- `core.*`:
  - `fileFingerprint` ignores the modification time and changes with the content;
  - `pathId` and `nodeForPathId` round-trip every node of the fixture, and unknown ids give
    `NoNode`.
- `app.pmi_panel` (offscreen):
  - `UserView` JSON round trip (camera, hidden ids, planes);
  - `ModelDataStore`: save and load by fingerprint, a copied file finds the data, an edited
    file at the same path finds it and moves it, an unreadable file is reported;
  - `SavedViewsDialog`: rename, delete, reorder, empty names refused.
- `app.main_window`: save a view with a hidden part, a section plane and a turned camera,
  change all three, restore the view and get them back; close and reopen the file and the view
  is still listed.
