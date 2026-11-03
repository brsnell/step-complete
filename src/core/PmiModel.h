#pragma once

#include "core/Document.h"
#include "core/PmiText.h"

#include <TDF_Label.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

#include <optional>
#include <string>
#include <vector>

// Product and Manufacturing Information (GD&T) read from an XCAF document.
//
// PmiModel turns OCCT's XCAF GD&T structures (XCAFDoc_DimTolTool, ViewTool,
// ClippingPlaneTool) into plain value objects that the UI and the renderer use
// without touching XCAF. Every annotation keeps:
//  - its semantics (type, values, tolerance, modifiers, datum references) as
//    display text and as a full property list,
//  - its graphical presentation from the file (polylines / tessellation), when present,
//  - the geometry it applies to (target faces/edges), and
//  - the part it is defined in, so the viewer can place it on every instance.
// Shapes are in the coordinate system of that owning part (prototype space).

namespace sc::core {

using PmiId = int;
constexpr PmiId kNoPmi = -1;

enum class PmiKind {
  Dimension,
  GeometricTolerance,
  Datum,
  DatumTarget,
};

const char* toString(PmiKind kind);

struct PmiItem {
  PmiId id = kNoPmi;
  PmiKind kind = PmiKind::Dimension;
  TDF_Label label;
  // Further XCAF labels merged into this item. OCCT creates one datum label per
  // tolerance that references a datum; they are merged into one item per datum.
  std::vector<TDF_Label> aliases;

  std::string name;               // presentation name from the file, else a generated one ("Flatness 2")
  std::string typeName;           // "Linear distance", "Position", "Datum", ...
  std::string text;               // "⌀12.70 ±0.05", "⌖ | ⌀0.1 Ⓜ | A | B", "A"
  std::vector<std::string> frame; // feature control frame cells (tolerances only)
  std::vector<pmi::Property> properties;

  // False when the file carries only a graphical presentation that OCCT did
  // not interpret semantically ("present but not interpreted").
  bool semantic = true;

  // Geometry the annotation applies to. Dimensions between two features use both lists.
  std::vector<TDF_Label> targetLabels;
  std::vector<TopoDS_Shape> targets;
  std::vector<TDF_Label> secondTargetLabels;
  std::vector<TopoDS_Shape> secondTargets;
  // The part (prototype shape label) the annotation is defined in; null if unknown.
  TDF_Label ownerPart;

  // Graphical presentation from the file (edges and/or triangulated faces).
  TopoDS_Shape presentation;
  std::string presentationName;
  std::optional<gp_Ax2> plane;
  std::optional<gp_Pnt> textPosition;
  std::optional<gp_Pnt> point1, point2; // dimension attachment points

  std::vector<PmiId> datums; // tolerances: referenced datums, primary first
};

struct PmiSavedView {
  int id = -1;
  TDF_Label label;
  std::string name;

  bool perspective = false;
  gp_Pnt projectionPoint; // eye position
  gp_Dir viewDirection;
  gp_Dir upDirection;
  double zoom = 1.0;
  double windowWidth = 0.0; // view window size in model units, if given
  double windowHeight = 0.0;
  std::optional<double> frontPlaneDistance;
  std::optional<double> backPlaneDistance;
  std::vector<gp_Pln> clippingPlanes;

  // Shapes shown in the view (prototype labels); empty means "all".
  std::vector<TDF_Label> shapes;
  // PMI shown in the view.
  std::vector<PmiId> pmi;
};

class PmiModel {
public:
  explicit PmiModel(const DocumentPtr& document, int decimals = 3);

  const std::vector<PmiItem>& items() const { return m_items; }
  const PmiItem& item(PmiId id) const { return m_items.at(static_cast<size_t>(id)); }
  const std::vector<PmiSavedView>& views() const { return m_views; }

  bool isEmpty() const { return m_items.empty() && m_views.empty(); }
  int count(PmiKind kind) const;
  int uninterpretedCount() const;

  // PMI attached to the given shape label or any of its sub-shape labels.
  std::vector<PmiId> itemsTargeting(const TDF_Label& shapeLabel) const;
  // The item read from this XCAF label (or merged from it), or kNoPmi.
  PmiId itemFor(const TDF_Label& label) const;

private:
  void readDimensions();
  void readTolerances();
  void readDatums();
  void readViews();

  DocumentPtr m_document;
  int m_decimals;
  std::vector<PmiItem> m_items;
  std::vector<PmiSavedView> m_views;
};

} // namespace sc::core
