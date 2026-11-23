#pragma once

#include "core/AssemblyIndex.h"
#include "core/OccHandle.h"
#include "core/StepImporter.h"

#include <Poly_Triangulation.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sc::core {

// Measure > Wall Thickness (docs/designs/wall-thickness.md): thickness by ray casting on the mesh.

class MeshBvh; // bounding-volume hierarchy over a map's triangles

// One part definition's thickness map, in the part's own coordinates.
struct ThicknessMap {
  OccHandle<Poly_Triangulation> mesh; // the refined display mesh, outward normals per node
  std::vector<float> thickness;       // per node (node i at [i - 1]); NaN where the ray found no exit
  std::shared_ptr<const MeshBvh> bvh; // for the readout
  double min = 0.0;                   // over the nodes with a value, leaving out the extreme 0.1% at each end
  double max = 0.0;
  int valued = 0; // nodes with a value
};

struct ThicknessOptions {
  double refineFraction = 1.0 / 150.0; // split triangles with an edge longer than this part of the size
  int refineLevels = 4;
  int maxPoints = 300000;
};

// Thickness map of one shape from its existing triangulation (faces without one are skipped).
// Null when the shape has no mesh. Blocking; points are processed in parallel.
std::shared_ptr<const ThicknessMap> computeThickness(const TopoDS_Shape& shape,
                                                     const ThicknessOptions& options = {},
                                                     const CancelToken* cancel = nullptr);

// The thickness under a point of the map's surface seen along 'direction' (both in the map's
// coordinates): finds the surface there, then goes into the part. nullopt off the part or with no
// exit.
std::optional<double> thicknessAt(const ThicknessMap& map, const gp_Pnt& point, const gp_Dir& direction);

struct ThicknessResult {
  std::vector<NodeId> parts;                                       // the instances analysed
  std::map<std::string, std::shared_ptr<const ThicknessMap>> maps; // by AssemblyIndex::prototypeId
  double min = 0.0;                                                // over every map with values
  double max = 0.0;
  int definitions = 0;  // part definitions analysed
  int withoutValue = 0; // definitions that have no thickness (sheets, no mesh)
  long long points = 0; // mesh points over every map
  long long valued = 0; // those with a value
  bool cancelled = false;

  std::shared_ptr<const ThicknessMap> mapOf(const AssemblyIndex& index, NodeId part) const;
};

// Analyses the given part instances (assemblies expanded), once per definition.
ThicknessResult analyseThickness(const AssemblyIndex& index, const std::vector<NodeId>& nodes,
                                 const ThicknessOptions& options = {}, const ProgressCallback& progress = {},
                                 const CancelToken* cancel = nullptr);

} // namespace sc::core
