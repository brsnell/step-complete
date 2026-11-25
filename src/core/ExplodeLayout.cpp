#include "core/ExplodeLayout.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Precision.hxx>

#include <map>
#include <optional>
#include <string>

namespace sc::core {

namespace {

std::optional<gp_Pnt> centerOf(const Bnd_Box& box) {
  if (box.IsVoid()) {
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
      Bnd_Box box;
      const TopoDS_Shape shape = index.prototypeShape(part);
      if (!shape.IsNull()) {
        BRepBndLib::Add(shape, box, true);
      }
      it = definitionBoxes.emplace(definition, box).first;
    }
    if (!it->second.IsVoid()) {
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
