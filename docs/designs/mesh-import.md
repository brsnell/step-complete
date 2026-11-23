# Mesh import (STL, OBJ, glTF)

Status: APPROVED (2026-11-23)

## What

STEP Complete opens mesh files the way it opens STEP and IGES: STL (`.stl`, ASCII or binary), OBJ
(`.obj` with its `.mtl` colors) and glTF 2.0 (`.gltf`, `.glb`). They display, section, export and
take part in the assembly tree, visibility, transparency, saved views, the parts list and the wall
thickness view. Mass properties come from the triangles. The tools that need exact geometry
(the measure modes, the geometry check and the clash check) are turned off for a mesh file, and
the status bar says why.

## Why

People are often sent meshes (3D-printing STL, OBJ and glTF from visualization tools), and other
viewers open them (TODOS v1.2, the second half of "other import formats" after IGES).

## Decisions

1. **What measuring does on a mesh: off, and it says so.** A mesh has no faces, edges or circles
   in the CAD sense: a face of an STL file is one triangle soup with no surface. Distance, angle,
   radius and entity info pick exact edges and faces and snap to circle centres, so on a mesh
   they would either fail or give numbers that look exact and are not. For a mesh file the
   Measure modes, Check Geometry and Check Clashes are disabled, and the status bar says
   "Measuring exact geometry, Check Geometry and Check Clashes are off for mesh files (STL); Mass
   Properties and Wall Thickness use the triangles." (with the format) when the file opens and
   when a measure mode is chosen. Picking a
   section plane's face on a mesh is rejected the same way ("not a flat face").
2. **Mass properties from the mesh.** Area always; volume, centroid and inertia by the
   divergence theorem over the triangles (OCCT's `BRepGProp` with triangulation), which is exact
   for a closed mesh and meaningless for an open one. The result says "From: mesh (exact only
   for closed meshes)". Materials and mass work as for solids, and the parts list uses the same
   numbers.
3. **Readers.** OCCT's `RWStl` for STL (one part named after the file), `RWObj_CafReader` and
   `RWGltf_CafReader` for OBJ and glTF, which fill the XCAF document directly with names,
   colors and the glTF node hierarchy. Every mesh is a face that holds only a triangulation,
   OCCT's usual form for meshes in a document, so display, sections, export and wall thickness
   need no change. OBJ and glTF colors arrive as visual materials, which the viewer would draw
   with physically based settings that wash them out and which the assembly tree does not read:
   each becomes the plain surface color (with its alpha) that STEP parts carry.
4. **Units and axes.** glTF is metres and Y up by specification: it is scaled to millimetres and
   turned Z up. STL and OBJ carry no unit: millimetres are assumed and the file unit reads
   "mm (assumed)". STL stays as it is (Z up, as 3D-printing tools write it); OBJ is read as Y up,
   like most tools write it and like this app's own OBJ export (Y up by default), so an exported
   OBJ opens the right way up.
5. **Detection.** By content where the format has a signature: a binary STL's triangle count
   matches its size, an ASCII STL starts with `solid` and has `facet`, a GLB starts with `glTF`,
   a `.gltf` is JSON with an `asset` member, an OBJ has `v` records. Otherwise the extension
   (`FileFormat.h`), as for STEP and IGES.
6. **Associations.** "Open with" only on every platform (these formats usually belong to a
   slicer or a 3D tool), plus the `model/stl`, `model/obj`, `model/gltf+json` and
   `model/gltf-binary` types on Linux.
7. **Everything else is shared.** The cache (content-keyed), progress, Cancel and the Open
   dialog filters.

## Out of scope

- Measuring on the mesh (vertex-to-vertex distance, fitted circles): revisit with user feedback.
- Other mesh formats (PLY, 3MF, VRML) and point clouds.
- Merging an STL's triangles into shells or solids, or repairing open meshes.

## Tests

- `core.MeshImport.*`:
  - a binary STL and an ASCII STL of a box read with the right triangles, a part named after
    the file, the unit "mm (assumed)" and mesh mass properties (volume of the box);
  - OBJ and glTF written by the app's own exporter read back with names, plain colors (no
    visual materials left), placement and the right axes (and glTF's metres become millimetres);
  - detection by content, both ways, and a mesh in the cache;
  - `planeOfFace` refuses a mesh face.
- `app.main_window`: an STL file opens, the measure modes and the checks are off with the
  reason, and mass properties report the box's volume from the mesh.
