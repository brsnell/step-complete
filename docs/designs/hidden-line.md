# Hidden-line display style

Status: DRAFT (written with the first implementation on 2026-11-21; awaiting approval)

## What

A fourth display style, **View > Hidden Line** (`L`), next to Shaded (`S`), Shaded with Edges
(`E`) and Wireframe (`W`). Faces are flat white, the sharp edges and the outlines of curved
surfaces are dark lines, and anything behind a face is hidden. The model reads like a drawing.

## Why

Hidden line was a v1 goal (PLAN §1) that was never built. Engineers use it to read shape without
color and shading, and it gives drawing-like image exports.

## Decisions

1. **GPU method, not OCCT's exact hidden-line removal.** OCCT's computed mode
   (`V3d_View::SetComputedMode`) projects the exact B-rep for each camera position and recomputes
   on every camera move. That is far too slow for large assemblies on the PLAN §2a reference
   machine. Instead, every face is drawn **unlit in white** and the depth buffer hides what is
   behind it. This is the same approach FreeCAD and most mesh viewers use.
2. **Outlines from OCCT's GPU silhouette** (`Graphic3d_Aspects::SetDrawSilhouette`). Face boundary
   edges alone leave cylinders and fillets without an outline. The silhouette pass draws them in
   the edge color, 1.5 px wide.
3. **Sharp (C0) edges only**, the same rule as Shaded with Edges. Tangent edges between fillets and
   faces are left out, and the silhouette covers the outline.
4. **White faces, not the background color.** The default background is a gradient, so faces in
   "background color" cannot match it. White also gives drawing-like exports on a white
   background.
5. **Part colors come back exactly.** Hidden line paints over the XCAF per-face styles. Leaving it
   rebuilds them from the document (`XCAFPrs_AISObject::DispatchStyles`), and part transparency is
   put back after the rebuild.
6. **Selection and hover highlight in their usual color.** Highlighting uses the context's own
   highlight styles and does not change.
7. **Section caps are white.** Caps take the part's material, so in hidden line they are white
   (and hatched when Hatch is on), as in a drawing.

## Cost

Measured on a 300-part benchmark model (`sc_make_bench_model`, 600 instances) at 1024×768 with
Mesa llvmpipe (software): about 80–88 ms per frame against 52–60 ms for Shaded with Edges, so about
1.5× the frame time. Switching to it takes 0.3–0.4 s, the same as the other styles. On a real GPU
the silhouette pass draws each part a second time. That still has to be checked on the reference
machine (TODOS: performance check).

## Out of scope

- Hidden edges drawn dashed (needs exact HLR).
- Tangent edges drawn thin.
- Line widths or colors in Preferences.

## Tests

- `render.viewport_smoke`: `SceneManagerTest::hiddenLineDrawsWhiteFacesAndRestoresColors` (white
  faces, no part color, exact restore including a transparent part),
  `ViewportSmokeTest::switchesDisplayStyles` and `SectionToolTest::capsFollowTheHiddenLineStyle`.
