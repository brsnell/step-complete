# PMI coverage

What STEP Complete shows from AP242 PMI, measured on the NIST MBE PMI test models
(public domain; fetched by `tests/data/fetch.sh`). CI prints these numbers for every OCCT build
(job summary "PMI extracted"), so this page can be checked against the current code.

## What works

| Feature | Status | Notes |
|---|---|---|
| Dimensions (size, location, angular, radius/diameter, …) | ✅ | Value, ± / asymmetric tolerance, limits, fit class, qualifiers, modifiers, decimals |
| Geometric tolerances (all 14 characteristics) | ✅ | Feature control frame with symbol, zone (⌀, S⌀, per-unit-length), material and zone modifiers, datum references in order |
| Datums and datum targets | ✅ | One datum per letter (OCCT's per-reference copies are merged); target type, number, size |
| Links to geometry | ✅ | Selecting PMI highlights the faces/edges it applies to, on every instance of the part |
| Graphical presentation from the file | ✅ OCCT 8 / ⚠️ 7.6 | Polylines and tessellated annotations, drawn as authored. OCCT 7.6 does not read tessellated presentations (most NIST files) |
| Semantic-only rendering | ✅ | Screen-aligned boxed feature control frames, datum flags and dimension values at the annotation's text position (or the target's center), with a leader; labels sharing an anchor stack. Bundled STIX Two Math font covers every GD&T symbol. Labels at *nearby* anchors can still overlap; dimension extension lines and arrows are not drawn yet |
| Saved views | ⚠️ partial | Name, orientation, visible PMI, clipping planes. The camera **position** and window size are not usable (see gaps), so the view is fitted to the model in the saved orientation |
| Not-interpreted annotations | ✅ | Listed under "Not Interpreted" and drawn from their graphics, never hidden |

## Visual check (OCCT 8.0.1)

CI renders every model (isometric view and each saved view) with OCCT 8 and prints the images
to the Linux job log (`sc_pmi_snapshots`). Findings from that check:

- CTC-01, CTC-02, CTC-03, CTC-05: the authored graphical PMI shows completely and in place;
  saved views filter it correctly.
- Saved-view text can read mirrored when the view is not looked at from the file's camera
  position (see gap 1).
- FTC-06: annotations whose graphics OCCT reads as unlinked items (gap 7) also get boxed
  semantic labels, which pile up at their targets. Semantic labels are being made automatic
  (off when most items have file graphics).

## Per-model extraction

Counts after merging datum copies and duplicate presentations. "Targeted" is the number of items
linked to geometry. "Presentations" is the number of items with graphics from the file.

| Model | OCCT | Dimensions | Tolerances | Datums | Datum targets | Not interpreted | Saved views | Presentations | Targeted |
|---|---|---|---|---|---|---|---|---|---|
| CTC-01 | 7.6.3 | 12 | 6 | 3 | 0 | 4 | 1 | 0 | 19/21 |
| CTC-02 | 7.6.3 | 7 | 22 | 6 | 9 | 0 | 3 | 0 | 38/44 |
| CTC-03 | 7.6.3 | 10 | 12 | 5 | 0 | 1 | 1 | 0 | 22/27 |
| CTC-04 | 7.6.3 | 13 | 6 | 8 | 0 | 4 | 0 | 27 | 23/27 |
| CTC-05 | 7.6.3 | 8 | 10 | 2 | 2 | 2 | 2 | 22 | 18/22 |
| FTC-06 | 7.6.3 | 24 | 27 | 8 | 4 | 0 | 3 | 0 | 55/63 |
| FTC-07 | 7.6.3 | 27 | 25 | 5 | 0 | 10 | 0 | 57 | 47/57 |
| FTC-10 | 7.6.3 | 36 | 35 | 11 | 0 | 13 | 0 | 82 | 69/82 |
| CTC-01 | 8.0.1 | 16 | 6 | 3 | 0 | 8 | 1 | 21 | 23/25 |
| CTC-02 | 8.0.1 | 9 | 22 | 6 | 9 | 2 | 3 | 46 | 39/46 |
| CTC-03 | 8.0.1 | 12 | 12 | 5 | 0 | 3 | 1 | 28 | 24/29 |
| CTC-04 | 8.0.1 | 13 | 6 | 8 | 0 | 4 | 0 | 27 | 23/27 |
| CTC-05 | 8.0.1 | 8 | 10 | 2 | 2 | 2 | 2 | 22 | 18/22 |
| FTC-06 | 8.0.1 | 41 | 27 | 8 | 4 | 17 | 3 | 23 | 71/80 |
| FTC-07 | 8.0.1 | 27 | 25 | 5 | 0 | 10 | 0 | 57 | 47/57 |
| FTC-10 | 8.0.1 | 36 | 35 | 11 | 0 | 13 | 0 | 82 | 69/82 |

## Known gaps (OCCT's STEP reader)

Found by comparing the files' raw entities with what OCCT stores. All of them are candidates for
fixes upstream (OCCT is open source; patches would go through a vcpkg overlay port first).

1. **Saved-view camera position is dropped.** `STEPCAFControl_Reader` stores the view volume's
   projection point, which is relative to the camera's own coordinate system, and discards the
   camera's location (`CAMERA_MODEL_D3.view_reference_system.location`). The view window size is
   also not converted to the document unit. We use the saved orientation and fit the model.
2. **Lower tolerance sign is lost.** Plus/minus dimensions are stored as
   (nominal, |lower deviation|, upper deviation). A lower bound *above* the nominal (e.g. +0.05/+0.10)
   reads as −0.05. We always show the lower deviation as negative, which is correct for the common case.
3. **Geometric tolerance magnitude on OCCT 7.6.** Values written as complex
   `LENGTH_MEASURE_WITH_UNIT` instances read as 0 on 7.6 (e.g. CTC-01's flatness 0.2 mm). OCCT 8 reads them.
4. **Tessellated presentations on OCCT 7.6** are not read (no graphics for CTC-01/02/03, FTC-06).
5. **Duplicated presentations.** OCCT sometimes stores an annotation's graphics a second time as a
   separate "dimension presentation". We fold these into the semantic item by name.
6. **Some datum features have no geometry link** (e.g. CTC-01 datums B and C on 7.6).
7. **Unlinked graphics on OCCT 8.** For some files (CTC-01, FTC-06) OCCT 8 reads tessellated
   graphics as separate items whose names do not match a semantic item, so they are listed under
   "Not Interpreted" (and drawn) next to the semantic item, which then has no graphics of its own.
   We fold such graphics into the semantic item when exactly one item without graphics applies to
   the same faces/edges (FTC-06: 23 → 17 left). The rest target different geometry than their
   semantic item; matching them needs the STEP draughting-callout links OCCT does not expose.

Values: OCCT converts lengths to millimeters and angles to degrees. Displayed text follows
ASME Y14.5 / ISO 1101 conventions with Unicode GD&T symbols.
