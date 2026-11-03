#pragma once

#include "core/AssemblyIndex.h"
#include "core/Measure.h"
#include "core/Units.h"

#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

#include <optional>
#include <string>
#include <utility>
#include <vector>

// Interactive measurement: turns picked entities into a formatted result and
// the witness geometry that shows what was measured. Pure core code (no Qt,
// no viewer), so the whole pipeline from picks to displayed text is testable.

namespace sc::core {

enum class MeasureMode {
  EntityInfo,   // 1 pick
  Distance,     // 2 picks: minimum distance between entities
  PointToPoint, // 2 picks: snapped points
  Angle,        // 2 picks
  Radius,       // 1 pick
  // Volume, area, centroid, inertia and bounding boxes of the selected parts. Works on a part
  // selection (see computeMassProperties), not on picks.
  MassProperties, // 0 picks
};

const char* toString(MeasureMode mode);
// Number of sub-shape picks a mode needs. 0 means the mode does not use picks.
int picksRequired(MeasureMode mode);

struct MeasureDisplayOptions {
  LengthUnit lengthUnit = LengthUnit::Millimeter;
  AngleUnit angleUnit = AngleUnit::Degree;
  int decimals = 3;
  int angleDecimals = 2;
};

// One picked entity, in world coordinates.
struct Pick {
  TopoDS_Shape shape; // vertex, edge or face, already placed with toWorld()
  gp_Pnt point;       // where the user clicked on it
  std::string owner;  // display path of the part instance, for the report
};

// Geometry that shows the work: drawn on top of the model.
struct Witness {
  std::vector<std::vector<gp_Pnt>> polylines;
  std::vector<gp_Pnt> markers;
  gp_Pnt labelAnchor;
  std::string label;
  bool empty() const { return polylines.empty() && markers.empty() && label.empty(); }
};

struct Measurement {
  MeasureMode mode = MeasureMode::EntityInfo;
  bool ok = false;
  std::string error;
  std::string headline; // the main value, e.g. "12.700 mm"
  std::vector<std::pair<std::string, std::string>> details;
  std::vector<std::string> entities; // what was picked, e.g. "Face (Plane) on Bracket"
  Witness witness;
};

enum class SnapKind { Vertex, Endpoint, Midpoint, Center, OnEdge, OnFace, Free };
const char* toString(SnapKind kind);

struct SnapResult {
  gp_Pnt point;
  SnapKind kind = SnapKind::Free;
};

// Snaps a click on an entity to a meaningful point: a vertex itself, the
// center of a circular edge, the nearest of an edge's ends and midpoint when
// within snapDistance (else the closest point on the edge), or the clicked
// point on a face. projectOnFace=false skips the exact projection onto faces
// (cheap enough to call on every mouse move).
SnapResult snap(const TopoDS_Shape& shape, const gp_Pnt& picked, double snapDistance,
                bool projectOnFace = true);
gp_Pnt snapPoint(const TopoDS_Shape& shape, const gp_Pnt& picked, double snapDistance);

// Short description of a picked entity, e.g. "Edge (Circle)".
std::string describeEntity(const TopoDS_Shape& shape);

// picks.size() must equal picksRequired(mode); otherwise the result is an error.
// snapDistance is used by PointToPoint (model units, typically a few pixels).
Measurement computeMeasurement(MeasureMode mode, const std::vector<Pick>& picks,
                               const MeasureDisplayOptions& options, double snapDistance = 0.0);

// One part instance that goes into a mass-properties measurement.
struct MassItem {
  TopoDS_Shape worldShape;                      // placed in world coordinates
  std::string name;                             // display path, for the entity list
  std::optional<ValidationProperties> declared; // what the STEP file claims (prototype coordinates)
  TopLoc_Location location;                     // instance placement: maps declared.centroid to world
};

// Mass properties (unit density) of the combined items: volume, area, centroid, principal
// moments of inertia, bounding boxes. With exactly one item that carries declared validation
// properties, the file's claims are compared with the computed values. The witness is the
// axis-aligned bounding box with a marker at the centroid.
Measurement computeMassProperties(const std::vector<MassItem>& items, const MeasureDisplayOptions& options);

// Readable multi-line text for the clipboard ("Distance: 12.700 mm" plus
// indented entities and details), and a CSV table (header + one row per
// measurement) for export.
std::string toClipboardText(const Measurement& measurement);
std::string toCsv(const std::vector<Measurement>& measurements);

} // namespace sc::core
