#include "core/ExplodeLayout.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Precision.hxx>
#include <TopExp_Explorer.hxx>

#include <map>
#include <optional>
#include <string>

namespace sc::core {

namespace {

// A box with a finite extent: not empty, and not open or whole (unbounded geometry, such as an
// untrimmed plane, would put the center near infinity and throw the parts that far).
bool isFinite(const Bnd_Box& box) {
  return !box.IsVoid() && !box.IsOpen() && !box.IsWhole();
}

// The box of a part definition. A definition can mix a solid with unbounded geometry (an
// untrimmed reference plane or line some exporters add): then the box of its finite faces, edges
// and vertices, so the part still has a center. Void when nothing in it is finite.
Bnd_Box finiteBox(const TopoDS_Shape& shape) {
  Bnd_Box whole;
  if (shape.IsNull()) {
    return whole;
  }
  BRepBndLib::Add(shape, whole, true);
  if (isFinite(whole)) {
    return whole;
  }
  Bnd_Box finite;
  for (const TopAbs_ShapeEnum kind : {TopAbs_FACE, TopAbs_EDGE, TopAbs_VERTEX}) {
    for (TopExp_Explorer it(shape, kind); it.More(); it.Next()) {
      Bnd_Box piece;
      BRepBndLib::Add(it.Current(), piece, true);
      if (isFinite(piece)) {
        finite.Add(piece);
      }
    }
  }
  return finite;
}

std::optional<gp_Pnt> centerOf(const Bnd_Box& box) {
  if (!isFinite(box)) {
    return std::nullopt;
  }
  const gp_Pnt low = box.CornerMin();
  const gp_Pnt high = box.CornerMax();
  return gp_Pnt((low.XYZ() + high.XYZ()) / 2.0);
}

} // namespace

ExplodeLayout::ExplodeLayout(const AssemblyIndex& index) {
  const std::vector<AssemblyNode>& nodes = index.nodes();
  _directions.assign(nodes.size(), gp_Vec(0.0, 0.0, 0.0));

  // World boxes: one per part definition, moved to each instance; an assembly's box holds its
  // children's. Nodes are in tree order (a parent before its children), so going backwards
  // finishes every child before its parent.
  std::map<std::string, Bnd_Box> definitionBoxes;
  std::vector<Bnd_Box> boxes(nodes.size());
  for (const NodeId part : index.partInstances()) {
    const std::string definition = index.prototypeId(part);
    auto it = definitionBoxes.find(definition);
    if (it == definitionBoxes.end()) {
      it = definitionBoxes.emplace(definition, finiteBox(index.prototypeShape(part))).first;
    }
    if (isFinite(it->second)) {
      boxes[static_cast<size_t>(part)] = it->second.Transformed(index.node(part).location.Transformation());
    }
  }
  for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
    if (it->parent != NoNode && !boxes[static_cast<size_t>(it->id)].IsVoid()) {
      boxes[static_cast<size_t>(it->parent)].Add(boxes[static_cast<size_t>(it->id)]);
    }
  }
  Bnd_Box model;
  for (const NodeId root : index.roots()) {
    model.Add(boxes[static_cast<size_t>(root)]);
  }
  const std::optional<gp_Pnt> modelCenter = centerOf(model);

  for (const AssemblyNode& node : nodes) {
    const std::optional<gp_Pnt> center = centerOf(boxes[static_cast<size_t>(node.id)]);
    const std::optional<gp_Pnt> parentCenter =
        node.parent == NoNode ? modelCenter : centerOf(boxes[static_cast<size_t>(node.parent)]);
    gp_Vec direction =
        node.parent == NoNode ? gp_Vec(0.0, 0.0, 0.0) : _directions[static_cast<size_t>(node.parent)];
    if (center && parentCenter) {
      direction += gp_Vec(*parentCenter, *center);
    }
    _directions[static_cast<size_t>(node.id)] = direction;
    if (node.kind == NodeKind::Part && direction.Magnitude() > Precision::Confusion()) {
      _canExplode = true;
    }
  }
}

gp_Vec ExplodeLayout::offset(NodeId node, double factor) const {
  if (node < 0 || static_cast<size_t>(node) >= _directions.size()) {
    return gp_Vec(0.0, 0.0, 0.0);
  }
  return _directions[static_cast<size_t>(node)] * factor;
}

std::vector<gp_Vec> ExplodeLayout::offsets(double factor) const {
  std::vector<gp_Vec> result;
  result.reserve(_directions.size());
  for (const gp_Vec& direction : _directions) {
    result.push_back(direction * factor);
  }
  return result;
}

} // namespace sc::core
