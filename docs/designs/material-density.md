# Material density for mass properties

Status: APPROVED (2026-11-22)

## What

- **Measure > Set Material...** gives the selected parts a material: one from a short list
  (steel, stainless steel, aluminium, titanium, brass, copper, cast iron, ABS, PLA, nylon,
  polycarbonate, oak) or a custom name and density.
- **Measure > Clear Material** removes it, so the part goes back to the material the file
  declares, if any.
- **Measure > Mass Properties** then reports the mass, the centre of mass and the moments of
  inertia with each part's own density.
- **Measure > Mass Unit** picks grams, kilograms or pounds.
- The Properties panel shows a part's material.

## Why

Mass properties are computed at unit density only, so the app cannot answer "how heavy is this
assembly" (TODOS v1.1).

## Decisions

1. **A material belongs to the part definition, not the instance.** All 20 instances of a bolt
   are the same steel, so setting a material on one instance (or an assembly) applies to every
   instance of the parts below it.
2. **Where the density comes from, first match wins:**
   1. the material the user set;
   2. the material the STEP file declares for the part (OCCT's `XCAFDoc_MaterialTool`; it hands
      over densities in g/mm³ whatever the file's length unit, checked by a round trip through
      STEP in millimetres and inches). OCCT's reader can mistake one of the part's user-defined
      attribute names for the material name (seen with OCCT 8), so a name equal to one of the
      part's attribute names is dropped and only the density kept;
   3. none.
3. **Mass is weighted part by part.** Each solid's volume properties are added to the total
   with its own density (`GProp_GProps::Add`), so the mass, the centre of mass and the inertia
   are right for mixed materials. It is never one density times the total volume.
4. **All or nothing.** Mass, centre of mass and mass moments are shown only when every measured
   part with volume has a density. Otherwise the result keeps the unit-density values it shows
   today and adds "Mass: no material for 3 of 5 parts". A partial mass would look like the
   answer and be wrong.
5. **What is shown.**
   - Mass becomes the headline, in the mass unit.
   - Volume, area and the bounding boxes stay.
   - "Centroid" becomes "Center of mass".
   - The principal moments are in mass unit × length unit² (for example kg·mm²).
   - With a single density, the result shows the density; with a single material name, it
     shows that too.
   - CSV export carries all of these as before, units included.
6. **Units.** Densities are stored and typed in kg/m³; the dialog also shows g/cm³. Mass unit:
   g, kg (default) or lb, saved like the length unit.
7. **Storage.** User materials go in the per-file `ModelDataStore` document from user saved
   views, under `"materials"`, keyed by the part definition's XCAF label entry:
   `{"0:1:1:1": {"name": "Steel", "density": 7850}}`.
8. **Code layout.**
   - `core`:
     - `measure::massProperties(shapes, densities)`;
     - `MassItem::density` and `MassItem::material`;
     - `MassUnit` with `formatMass`;
     - `AssemblyIndex::prototypeId` and `fileMaterial`.
   - `app`:
     - `MaterialDialog`;
     - the menu items, the stored materials and the Properties row in `MainWindow` and
       `PropertiesPanel`.

## Out of scope

- A user-editable material library, and densities that vary within a part.
- Writing materials back into the STEP file.
- Mass in the parts list (the BOM export item will use these densities).

## Tests

- `core.*`:
  - weighted mass properties: two boxes of different densities give the right mass, centre of
    mass and inertia, and a unit-density call is unchanged;
  - `computeMassProperties`: the headline mass and units, the all-or-nothing rule, and pounds
    and grams;
  - `fileMaterial` reads a material through a STEP round trip;
  - `formatMass`.
- `app.pmi_panel` (offscreen): `MaterialDialog` presets, custom values and its result.
- `app.main_window`:
  - set a material on the plate, then mass properties of the plate gives its mass;
  - without materials, mass properties of the fixture says how many parts lack one;
  - the material is still set after reopening the file.
