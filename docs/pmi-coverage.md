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
| Semantic-only rendering | ✅ | Screen-aligned boxed feature control frames, datum flags and dimension values at the annotation's text position (or the target's center), with a leader; labels sharing an anchor stack. Bundled STIX Two Math font covers every GD&T symbol. Linear dimensions get extension lines, a dimension line through the text position and constant-pixel-size arrowheads in the annotation plane (between the targets when the file has no placement); diameter/radius get an arrowed leader or a diameter/radius line across the circular feature; angular get arrowed leaders to the attachment points. `PmiScene::declutter(view)` moves overlapping labels apart for the current camera (greedy, screen space, call after fit; not per frame) with connectors to the anchors. Labels are on by default only when fewer than half of the items have file graphics (`setSemanticLabelsVisible`); items neither interpreted nor graphical draw nothing. Limits: angular dimensions have no arc; declutter is for the camera at the time of the call |
| Saved views | ✅ parallel / ⚠️ central | Name, visible PMI, clipping planes and the camera: location, orientation and view window come from the STEP `CAMERA_MODEL_D3` (read by `StepImporter`, see gap 1), so the view shows the part as the exporter framed it. Parallel projection is verified on the NIST files; central projection (field of view from the window and plane distance) is implemented but untested, as no NIST file has one. Front/back plane distances are kept but not applied; the depth range is fitted to the model. Views without a STEP camera fall back to the saved orientation fitted to the model |
| Not-interpreted annotations | ✅ | Listed under "Not Interpreted" and drawn from their graphics, never hidden |

## Visual check (OCCT 8.0.1)

CI renders every model (isometric view and each saved view) with OCCT 8 and prints the images
to the Linux job log (`sc_pmi_snapshots`). Findings from that check:

- CTC-01, CTC-02, CTC-03, CTC-05: the authored graphical PMI shows completely and in place;
  saved views filter it correctly.
- Saved views now use the file's camera (gap 1 fixed), so text authored in the annotation planes
  faces the viewer as intended. Some views are framed off-center as exported (e.g. FTC-06 MBD_A);
  we keep the authored framing.
- FTC-06: annotations whose graphics OCCT reads as unlinked items (gap 7) used to get boxed
  semantic labels as well, piling up at their targets. Semantic labels are now off by default when
  most items have file graphics.

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

1. **Saved-view camera position is dropped by OCCT (worked around).** `STEPCAFControl_Reader`
   stores the view volume's projection point, which is relative to the camera's own coordinate
   system, and discards the camera's location (`CAMERA_MODEL_D3.view_reference_system.location`).
   The view window size is also not converted to the document unit. `StepImporter` therefore walks
   the STEP entities itself and reads each camera (location, axis, reference direction, view
   volume, window), converting lengths with the unit of the file's geometric representation
   context (`STEPControl_Reader::FileUnits` misses contexts that also carry a global uncertainty,
   as most CAD systems write them, so it is not used here). `PmiModel` matches cameras to the XCAF
   views by name, then by file order.

   Semantics as verified on the NIST files: the camera frame is the view reference system
   (X = reference direction, Z = axis, Y = Z x X); the camera looks along **+Z**, towards the model,
   and the view plane is `view_plane_distance` along it. In CTC-01, CTC-02/C and FTC-06/C
   `location + view_plane_distance * axis` lands within a few tens of mm of the model's centre
   (CTC-01: `|location|` = 1645.003 = `view_plane_distance`, axis pointing at the origin), and the
   `PLANAR_BOX` is centred on that point. For parallel projection the window gives the visible
   height (`Graphic3d_Camera::SetScale`), widened if the viewport is wider than the window.

   Still approximate: some NIST views are panned so that the part sits off-centre or is partly
   outside the window (CTC-02 A and FTC-06 A: about 10 to 25 percent of the part is cropped); this
   is how the files define them, we do not re-centre. The window's own off-centre placement is
   honoured for parallel views only. Models with no XCAF view (CTC-04, FTC-07/10 on OCCT 7.6) have
   no view to apply.
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
