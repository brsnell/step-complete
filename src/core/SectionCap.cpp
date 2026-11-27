#include "core/SectionCap.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>

#include <cmath>
#include <limits>

namespace sc::core {

namespace {

// Distances along the ray and point classification (mm).
constexpr double Tolerance = 1e-5;

// Signed distance of a point from the plane, positive on the cut-away side.
double signedDistance(const CutPlane& cut, const gp_Pnt& point) {
  const gp_Dir& normal = cut.plane.Axis().Direction();
  return gp_Vec(cut.plane.Location(), point).Dot(gp_Vec(normal));
}

bool insideASolid(const TopoDS_Shape& part, const gp_Pnt& point) {
  for (TopExp_Explorer it(part, TopAbs_SOLID); it.More(); it.Next()) {
    BRepClass3d_SolidClassifier classifier(it.Current(), point, Tolerance);
    const TopAbs_State state = classifier.State();
    if (state == TopAbs_IN || state == TopAbs_ON) {
      return true;
    }
  }
  return false;
}

// Keeps the part of a convex polygon on the kept side of a plane (Sutherland-Hodgman).
std::vector<gp_Pnt> clipPolygon(const std::vector<gp_Pnt>& polygon, const CutPlane& cut) {
  std::vector<gp_Pnt> kept;
  for (size_t i = 0; i < polygon.size(); ++i) {
    const gp_Pnt& a = polygon[i];
    const gp_Pnt& b = polygon[(i + 1) % polygon.size()];
    const double da = signedDistance(cut, a);
    const double db = signedDistance(cut, b);
    if (da <= 0.0) {
      kept.push_back(a);
    }
    if ((da < 0.0 && db > 0.0) || (da > 0.0 && db < 0.0)) {
      const double t = da / (da - db);
      kept.push_back(gp_Pnt(a.XYZ() + (b.XYZ() - a.XYZ()) * t));
    }
  }
  return kept;
}

} // namespace

std::optional<CapHit> capHit(const TopoDS_Shape& part, const gp_Pnt& hitPoint, const gp_Dir& viewDirection,
                             const std::vector<CutPlane>& planes) {
  // Walking back from the hit towards the eye, the first plane whose cut side we reach is where
  // the visible model starts: the ray entered the kept side of every plane there.
  const gp_Vec direction(viewDirection);
  double nearest = std::numeric_limits<double>::infinity();
  std::optional<size_t> entered;
  for (size_t i = 0; i < planes.size(); ++i) {
    const double along = gp_Vec(planes[i].plane.Axis().Direction()).Dot(direction);
    if (along > -1e-9) {
      continue; // the ray does not come from this plane's cut side
    }
    const double back = signedDistance(planes[i], hitPoint) / along; // distance back to the plane
    if (back < nearest) {
      nearest = back;
      entered = i;
    }
  }
  if (!entered || nearest <= Tolerance || !planes[*entered].capped) {
    return std::nullopt; // no plane in front, the hit is on the plane, or no cap is drawn
  }
  const gp_Pnt crossing = hitPoint.Translated(-direction * nearest);
  if (!insideASolid(part, crossing)) {
    return std::nullopt; // the ray crossed the plane outside this part: the hit is what is seen
  }
  return CapHit{*entered, crossing};
}

TopoDS_Shape capFaces(const TopoDS_Shape& part, const std::vector<CutPlane>& planes, size_t index) {
  if (index >= planes.size() || part.IsNull()) {
    return {};
  }
  TopoDS_Compound solids;
  BRep_Builder builder;
  builder.MakeCompound(solids);
  bool anySolid = false;
  for (TopExp_Explorer it(part, TopAbs_SOLID); it.More(); it.Next()) {
    builder.Add(solids, it.Current());
    anySolid = true;
  }
  if (!anySolid) {
    return {};
  }
  Bnd_Box box;
  BRepBndLib::Add(solids, box);
  if (box.IsVoid()) {
    return {};
  }

  // A square on the plane larger than the part, trimmed by the other planes.
  const gp_Pln& plane = planes[index].plane;
  double xmin, ymin, zmin, xmax, ymax, zmax;
  box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
  const gp_Pnt center((xmin + xmax) / 2.0, (ymin + ymax) / 2.0, (zmin + zmax) / 2.0);
  const double halfSize = std::sqrt(box.SquareExtent()) + 1.0;
  const gp_Vec normal(plane.Axis().Direction());
  const gp_Pnt onPlane = center.Translated(-normal * gp_Vec(plane.Location(), center).Dot(normal));
  const gp_Vec u = gp_Vec(plane.Position().XDirection()) * halfSize;
  const gp_Vec v = gp_Vec(plane.Position().YDirection()) * halfSize;
  std::vector<gp_Pnt> polygon = {onPlane.Translated(-u - v), onPlane.Translated(u - v),
                                 onPlane.Translated(u + v), onPlane.Translated(-u + v)};
  for (size_t j = 0; j < planes.size() && polygon.size() >= 3; ++j) {
    if (j != index) {
      polygon = clipPolygon(polygon, planes[j]);
    }
  }
  if (polygon.size() < 3) {
    return {};
  }
  BRepBuilderAPI_MakePolygon outline;
  for (const gp_Pnt& corner : polygon) {
    outline.Add(corner);
  }
  outline.Close();
  if (!outline.IsDone()) {
    return {};
  }
  BRepBuilderAPI_MakeFace face(gp_Pln(onPlane, plane.Axis().Direction()), outline.Wire(), true);
  if (!face.IsDone()) {
    return {};
  }

  BRepAlgoAPI_Common common(face.Face(), solids);
  if (!common.IsDone()) {
    return {};
  }
  TopoDS_Compound faces;
  builder.MakeCompound(faces);
  bool anyFace = false;
  for (TopExp_Explorer it(common.Shape(), TopAbs_FACE); it.More(); it.Next()) {
    builder.Add(faces, it.Current());
    anyFace = true;
  }
  return anyFace ? TopoDS_Shape(faces) : TopoDS_Shape();
}

std::optional<TopoDS_Face> faceAt(const TopoDS_Shape& faces, const gp_Pnt& point) {
  if (faces.IsNull()) {
    return std::nullopt;
  }
  for (TopExp_Explorer it(faces, TopAbs_FACE); it.More(); it.Next()) {
    const TopoDS_Face& face = TopoDS::Face(it.Current());
    BRepClass_FaceClassifier classifier(face, point, Tolerance);
    const TopAbs_State state = classifier.State();
    if (state == TopAbs_IN || state == TopAbs_ON) {
      return face;
    }
  }
  return std::nullopt;
}

SnapResult snapOnCap(const TopoDS_Face& face, const gp_Pnt& picked, double snapDistance) {
  std::optional<SnapResult> best;
  const auto consider = [&best, &picked, snapDistance](const SnapResult& candidate) {
    const double distance = candidate.point.Distance(picked);
    if (distance <= snapDistance && (!best || distance < best->point.Distance(picked))) {
      best = candidate;
    }
  };
  for (TopExp_Explorer it(face, TopAbs_VERTEX); it.More(); it.Next()) {
    consider({BRep_Tool::Pnt(TopoDS::Vertex(it.Current())), SnapKind::Vertex});
  }
  if (best) {
    return *best;
  }
  for (TopExp_Explorer it(face, TopAbs_EDGE); it.More(); it.Next()) {
    const SnapResult special = snap(it.Current(), picked, snapDistance);
    if (special.kind != SnapKind::OnEdge) {
      consider(special);
    }
  }
  if (best) {
    return *best;
  }
  const TopoDS_Shape point = BRepBuilderAPI_MakeVertex(picked).Shape();
  for (TopExp_Explorer it(face, TopAbs_EDGE); it.More(); it.Next()) {
    BRepExtrema_DistShapeShape distance(point, it.Current());
    if (distance.IsDone() && distance.NbSolution() > 0) {
      consider({distance.PointOnShape2(1), SnapKind::OnEdge});
    }
  }
  return best ? *best : SnapResult{picked, SnapKind::OnFace};
}

} // namespace sc::core
