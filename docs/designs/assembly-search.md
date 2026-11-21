# Search in the assembly tree

Status: APPROVED (2026-11-21)

## What

A filter box above the Assembly tree, with **View > Find Part...** (`Ctrl+F`) to jump to it.
Typing shows only the rows whose instance name or product name contains the text, plus the
assemblies on the way to each match, expanded to show it.

## Why

Large assemblies (hundreds or thousands of parts) are hard to get around by scrolling. The PMI
panel already has **Filter PMI...**; the Assembly panel had nothing.

## Decisions

1. **Filter, not find-next.** The tree shrinks to the matches, the same as the PMI panel. A
   find-next search would leave the user scrolling a large tree.
2. **What matches:** instance name and product name (`AssemblyNode::name`, `prototypeName`). The
   match is a case-insensitive substring and works for non-ASCII text ("ø" finds "Ø"). File
   attributes such as part numbers are not searched yet.
3. **What stays:** a matching row, every assembly above it (Qt's recursive filtering), and
   everything below a matching assembly, so it can still be expanded to its parts.
4. **Expansion:** filtering expands exactly the rows that have a match below them; a matching
   assembly stays collapsed. Clearing the filter collapses back to the first level and scrolls to
   the selection.
5. **Selection follows the visible tree.** The tree selection drives the 3D selection, so a
   selected row that the filter hides is deselected in 3D too. Keeping it selected in 3D would make
   Hide, Isolate and Fit Selection act on parts the tree no longer lists.
6. **Opening another file clears the filter.**
7. **Code:** `AssemblyFilterProxy` (a `QSortFilterProxyModel` over `AssemblyTreeModel`) in
   `src/app`. `MainWindow` maps tree rows through it for selection sync.

## Out of scope

- Searching file attributes (part number, material) and PMI text.
- A "next match" button or match count.
- Selecting all matches in 3D with one command.

## Tests

- `app.pmi_panel` (offscreen): `AssemblyFilterTest`: empty and blank filters, path to a match,
  case-insensitive non-ASCII matching, a matching assembly keeping its parts, no matches.
- `app.main_window`: `filteringTheTreeExpandsToMatchesAndNarrowsTheSelection`: `Ctrl+F` focus,
  expansion, 3D deselection of a filtered-out row, clearing, and the reset when a file opens.
