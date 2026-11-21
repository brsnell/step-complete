# Measurements panel: clear on open, delete, show several at once

Status: done (PR #6, follow-ups in PR #7).

## Problems reported

1. Measurements from one file stayed in the list after opening another.
2. Measurements could not easily be deleted.
3. Only one measurement's witness was visible in the 3D view at a time.

## Decisions

- New results are shown in the 3D view right away (checked by default).
- Opening another file or closing the model clears the list silently, with no prompt.
- Closing the Measurements panel hides the witnesses; reopening it shows them again. A result
  that finishes while the panel is closed reopens it.
- `Backspace` deletes only on macOS; `Delete` (including numpad Delete) everywhere.
- The stale comment in `MainWindow::measureMassProperties` shipped as a disclosed note and was
  fixed in PR #7.

## Done

- Per-result checkboxes; every checked witness drawn, the current one emphasized
  (`MeasureTool::setWitnesses`, `MainWindow::syncMeasurementWitnesses`).
- `Delete` key and a context menu; removal in place.
- Clearing on open and close; Show All / Hide All.
- Witnesses rebuild after a device-pixel-ratio change (PR #7).

## Open

- PMI annotations keep the line widths of the screen they were loaded on
  (`PmiScene::setModel` fixes the device pixel ratio), the same problem PR #7 fixed for
  witnesses.
- No pixel test distinguishes an emphasized witness from a secondary one.
