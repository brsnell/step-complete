#include "render/PmiScene.h"

#include <AIS_Shape.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_Group.hxx>
#include <Poly_Polygon3D.hxx>
#include <Poly_Triangulation.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Select3D_SensitiveSegment.hxx>
#include <Select3D_SensitiveTriangle.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <SelectMgr_Selection.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>

#include <algorithm>

namespace sc::render {

namespace {

// Annotations: dark blue, readable on the light background and on part colors.
const Quantity_Color kPmiColor(0.05, 0.20, 0.55, Quantity_TOC_sRGB);
// Geometry a selected annotation applies to.
const Quantity_Color kTargetColor(1.0, 0.62, 0.10, Quantity_TOC_sRGB);
// Semantic labels whose anchors are closer than this (model units) are stacked.
constexpr double kSameAnchorDistance = 1e-3;

std::optional<gp_Pnt> centerOf(const std::vector<TopoDS_Shape>& shapes) {
  Bnd_Box box;
  for (const TopoDS_Shape& shape : shapes) {
    if (!shape.IsNull()) {
      BRepBndLib::Add(shape, box, false);
    }
  }
  if (box.IsVoid()) {
    return std::nullopt;
  }
  double xmin, ymin, zmin, xmax, ymax, zmax;
  box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
  return gp_Pnt((xmin + xmax) / 2.0, (ymin + ymax) / 2.0, (zmin + zmax) / 2.0);
}

// Points along an edge: its stored polyline (tessellated PMI has no curve) or a
// discretization of its curve.
std::vector<gp_Pnt> edgePoints(const TopoDS_Edge& edge) {
  std::vector<gp_Pnt> points;
  TopLoc_Location location;
  const OccHandle<Poly_Polygon3D> polygon = BRep_Tool::Polygon3D(edge, location);
  if (!polygon.IsNull()) {
    const gp_Trsf trsf = location.Transformation();
    for (int i = 1; i <= polygon->NbNodes(); ++i) {
      points.push_back(polygon->Nodes().Value(i).Transformed(trsf));
    }
    return points;
  }
  if (!BRep_Tool::IsGeometric(edge) || BRep_Tool::Degenerated(edge)) {
    return points;
  }
  const BRepAdaptor_Curve curve(edge);
  const GCPnts_TangentialDeflection sampler(curve, 0.2, 0.01);
  for (int i = 1; i <= sampler.NbPoints(); ++i) {
    points.push_back(sampler.Value(i));
  }
  return points;
}

} // namespace

// --- PmiPresentation -------------------------------------------------------------------

PmiPresentation::PmiPresentation(const core::PmiItem& item, const TopLoc_Location& placement, double dpr)
    : m_id(item.id), m_presentation(item.presentation), m_dpr(dpr > 0.0 ? dpr : 1.0) {
  SetLocalTransformation(placement.Transformation());

  // Semantic label for items without a graphical presentation.
  m_label = !item.text.empty() ? item.text : item.name;
  m_leaderTarget = item.point1 ? item.point1 : centerOf(item.targets);
  if (item.textPosition) {
    m_labelAnchor = *item.textPosition;
  } else if (m_leaderTarget) {
    m_labelAnchor = *m_leaderTarget;
  }
  if (m_leaderTarget && m_leaderTarget->Distance(m_labelAnchor) < 1e-9) {
    m_leaderTarget.reset();
  }
  collectGeometry();
}

void PmiPresentation::collectGeometry() {
  if (m_presentation.IsNull()) {
    return;
  }
  for (TopExp_Explorer it(m_presentation, TopAbs_EDGE); it.More(); it.Next()) {
    std::vector<gp_Pnt> points = edgePoints(TopoDS::Edge(it.Current()));
    if (points.size() >= 2) {
      m_polylines.push_back(std::move(points));
    }
  }
  for (TopExp_Explorer it(m_presentation, TopAbs_FACE); it.More(); it.Next()) {
    TopLoc_Location location;
    const OccHandle<Poly_Triangulation> mesh = BRep_Tool::Triangulation(TopoDS::Face(it.Current()), location);
    if (mesh.IsNull()) {
      continue;
    }
    const gp_Trsf trsf = location.Transformation();
    for (int i = 1; i <= mesh->NbTriangles(); ++i) {
      int n1 = 0, n2 = 0, n3 = 0;
      mesh->Triangle(i).Get(n1, n2, n3);
      m_triangles.push_back(mesh->Node(n1).Transformed(trsf));
      m_triangles.push_back(mesh->Node(n2).Transformed(trsf));
      m_triangles.push_back(mesh->Node(n3).Transformed(trsf));
    }
  }
}

void PmiPresentation::Compute(const OccHandle<PrsMgr_PresentationManager>&,
                              const OccHandle<Prs3d_Presentation>& prs, const int mode) {
  if (mode != 0) {
    return;
  }
  if (!m_polylines.empty()) {
    int vertices = 0;
    for (const auto& polyline : m_polylines) {
      vertices += static_cast<int>(polyline.size());
    }
    OccHandle<Graphic3d_Group> group = prs->NewGroup();
    group->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(kPmiColor, Aspect_TOL_SOLID, 1.0 * m_dpr));
    OccHandle<Graphic3d_ArrayOfPolylines> lines =
        new Graphic3d_ArrayOfPolylines(vertices, static_cast<int>(m_polylines.size()));
    for (const auto& polyline : m_polylines) {
      lines->AddBound(static_cast<int>(polyline.size()));
      for (const gp_Pnt& p : polyline) {
        lines->AddVertex(p);
      }
    }
    group->AddPrimitiveArray(lines);
  }
  if (!m_triangles.empty()) {
    OccHandle<Graphic3d_Group> group = prs->NewGroup();
    OccHandle<Graphic3d_AspectFillArea3d> fill = new Graphic3d_AspectFillArea3d();
    fill->SetInteriorStyle(Aspect_IS_SOLID);
    fill->SetInteriorColor(kPmiColor);
    fill->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
    fill->SetFaceCulling(Graphic3d_TypeOfBackfacingModel_DoubleSided);
    group->SetGroupPrimitivesAspect(fill);
    OccHandle<Graphic3d_ArrayOfTriangles> triangles =
        new Graphic3d_ArrayOfTriangles(static_cast<int>(m_triangles.size()));
    for (const gp_Pnt& p : m_triangles) {
      triangles->AddVertex(p);
    }
    group->AddPrimitiveArray(triangles);
  }
  if (!hasLabel()) {
    return;
  }

  // Semantic-only: the leader line; the text is a PmiLabel at the anchor.
  if (m_leaderTarget) {
    OccHandle<Graphic3d_Group> leader = prs->NewGroup();
    leader->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(kPmiColor, Aspect_TOL_SOLID, 1.0 * m_dpr));
    OccHandle<Graphic3d_ArrayOfSegments> segment = new Graphic3d_ArrayOfSegments(2);
    segment->AddVertex(m_labelAnchor);
    segment->AddVertex(*m_leaderTarget);
    leader->AddPrimitiveArray(segment);
  }
}

void PmiPresentation::ComputeSelection(const OccHandle<SelectMgr_Selection>& selection, const int mode) {
  if (mode != 0) {
    return;
  }
  OccHandle<SelectMgr_EntityOwner> owner = new SelectMgr_EntityOwner(this, 6);
  for (const auto& polyline : m_polylines) {
    for (size_t i = 1; i < polyline.size(); ++i) {
      selection->Add(new Select3D_SensitiveSegment(owner, polyline[i - 1], polyline[i]));
    }
  }
  for (size_t i = 0; i + 2 < m_triangles.size(); i += 3) {
    selection->Add(
        new Select3D_SensitiveTriangle(owner, m_triangles[i], m_triangles[i + 1], m_triangles[i + 2]));
  }
  if (hasLabel() && m_leaderTarget) {
    selection->Add(new Select3D_SensitiveSegment(owner, m_labelAnchor, *m_leaderTarget));
  }
}

// --- PmiScene --------------------------------------------------------------------------

PmiScene::PmiScene(const OccHandle<AIS_InteractiveContext>& context) : m_context(context) {}

PmiScene::~PmiScene() {
  clear();
}

std::vector<TopLoc_Location> PmiScene::placementsOf(const core::PmiItem& item) const {
  std::vector<TopLoc_Location> placements;
  if (m_index && !item.ownerPart.IsNull()) {
    for (const core::AssemblyNode& node : m_index->nodes()) {
      if (node.prototypeLabel == item.ownerPart) {
        placements.push_back(node.location);
      }
    }
  }
  if (placements.empty()) {
    placements.emplace_back(); // defined at the top level (or owner unknown)
  }
  return placements;
}

void PmiScene::setModel(std::shared_ptr<const core::PmiModel> pmi,
                        std::shared_ptr<const core::AssemblyIndex> index, double devicePixelRatio) {
  clear();
  m_pmi = std::move(pmi);
  m_index = std::move(index);
  if (!m_pmi) {
    return;
  }
  m_entries.resize(m_pmi->items().size());
  // Labels sharing an anchor are stacked: (anchor, labels so far).
  std::vector<std::pair<gp_Pnt, int>> anchors;
  for (const core::PmiItem& item : m_pmi->items()) {
    Entry& entry = m_entries[static_cast<size_t>(item.id)];
    for (const TopLoc_Location& placement : placementsOf(item)) {
      OccHandle<PmiPresentation> prs = new PmiPresentation(item, placement, devicePixelRatio);
      m_objectToPmi[prs.get()] = item.id;
      m_context->Display(prs, 0, 0, false);
      entry.placements.push_back(prs);
      if (prs->hasLabel()) {
        const gp_Pnt anchor = prs->labelAnchor().Transformed(placement.Transformation());
        auto same = std::find_if(anchors.begin(), anchors.end(), [&anchor](const auto& other) {
          return other.first.Distance(anchor) < kSameAnchorDistance;
        });
        if (same == anchors.end()) {
          anchors.emplace_back(anchor, 0);
          same = std::prev(anchors.end());
        }
        OccHandle<PmiLabel> label = new PmiLabel(item, anchor, same->second++, devicePixelRatio);
        m_objectToPmi[label.get()] = item.id;
        m_context->Display(label, 0, 0, false);
        entry.labels.push_back(label);
      }
    }
  }
}

void PmiScene::clear() {
  removeTargetHighlight();
  for (Entry& entry : m_entries) {
    for (const auto& prs : entry.placements) {
      m_context->Remove(prs, false);
    }
    for (const auto& label : entry.labels) {
      m_context->Remove(label, false);
    }
  }
  m_entries.clear();
  m_objectToPmi.clear();
  m_highlighted.clear();
  m_pmi.reset();
  m_index.reset();
}

void PmiScene::setVisible(const std::vector<core::PmiId>& ids, bool visible) {
  for (const core::PmiId id : ids) {
    if (id < 0 || id >= static_cast<core::PmiId>(m_entries.size())) {
      continue;
    }
    Entry& entry = m_entries[static_cast<size_t>(id)];
    if (entry.visible == visible) {
      continue;
    }
    entry.visible = visible;
    for (const auto& prs : entry.placements) {
      if (visible) {
        m_context->Display(prs, 0, 0, false);
      } else {
        m_context->Erase(prs, false);
      }
    }
    for (const auto& label : entry.labels) {
      if (visible) {
        m_context->Display(label, 0, 0, false);
      } else {
        m_context->Erase(label, false);
      }
    }
  }
}

void PmiScene::setOnlyVisible(const std::vector<core::PmiId>& ids) {
  std::vector<core::PmiId> hide;
  for (size_t i = 0; i < m_entries.size(); ++i) {
    if (std::find(ids.begin(), ids.end(), static_cast<core::PmiId>(i)) == ids.end()) {
      hide.push_back(static_cast<core::PmiId>(i));
    }
  }
  setVisible(hide, false);
  setVisible(ids, true);
}

bool PmiScene::isVisible(core::PmiId id) const {
  return id >= 0 && id < static_cast<core::PmiId>(m_entries.size()) &&
         m_entries[static_cast<size_t>(id)].visible;
}

std::vector<core::PmiId> PmiScene::visibleIds() const {
  std::vector<core::PmiId> ids;
  for (size_t i = 0; i < m_entries.size(); ++i) {
    if (m_entries[i].visible) {
      ids.push_back(static_cast<core::PmiId>(i));
    }
  }
  return ids;
}

core::PmiId PmiScene::pmiOf(const OccHandle<AIS_InteractiveObject>& object) const {
  const auto it = m_objectToPmi.find(object.get());
  return it == m_objectToPmi.end() ? core::kNoPmi : it->second;
}

std::vector<core::PmiId> PmiScene::selectedPmi() const {
  std::vector<core::PmiId> ids;
  for (m_context->InitSelected(); m_context->MoreSelected(); m_context->NextSelected()) {
    const core::PmiId id = pmiOf(m_context->SelectedInteractive());
    if (id != core::kNoPmi && std::find(ids.begin(), ids.end(), id) == ids.end()) {
      ids.push_back(id);
    }
  }
  return ids;
}

void PmiScene::removeTargetHighlight() {
  if (!m_targetHighlight.IsNull()) {
    m_context->Remove(m_targetHighlight, false);
    m_targetHighlight.Nullify();
  }
}

void PmiScene::highlight(const std::vector<core::PmiId>& ids) {
  removeTargetHighlight();
  m_highlighted = ids;
  m_context->ClearSelected(false);
  if (!m_pmi) {
    return;
  }

  TopoDS_Compound targets;
  BRep_Builder builder;
  builder.MakeCompound(targets);
  bool any = false;
  for (const core::PmiId id : ids) {
    if (id < 0 || id >= static_cast<core::PmiId>(m_entries.size())) {
      continue;
    }
    const core::PmiItem& item = m_pmi->item(id);
    const std::vector<TopLoc_Location> placements = placementsOf(item);
    for (const auto* shapes : {&item.targets, &item.secondTargets}) {
      for (const TopoDS_Shape& shape : *shapes) {
        for (const TopLoc_Location& placement : placements) {
          if (!shape.IsNull()) {
            builder.Add(targets, shape.Moved(placement));
            any = true;
          }
        }
      }
    }
    const Entry& entry = m_entries[static_cast<size_t>(id)];
    for (const auto& prs : entry.placements) {
      if (m_context->IsDisplayed(prs)) {
        m_context->AddOrRemoveSelected(prs, false);
      }
    }
    for (const auto& label : entry.labels) {
      if (m_context->IsDisplayed(label)) {
        m_context->AddOrRemoveSelected(label, false);
      }
    }
  }
  if (!any) {
    return;
  }
  OccHandle<AIS_Shape> shape = new AIS_Shape(targets);
  shape->SetColor(kTargetColor);
  shape->SetDisplayMode(AIS_Shaded);
  shape->SetWidth(3.0);
  // Drawn over the coplanar part faces it marks.
  shape->Attributes()->SetupOwnShadingAspect();
  const OccHandle<Graphic3d_AspectFillArea3d>& fill = shape->Attributes()->ShadingAspect()->Aspect();
  fill->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit); // flat color, readable from any angle
  fill->SetInteriorColor(kTargetColor);
  fill->SetPolygonOffsets(Aspect_POM_Fill, -2.0f, -2.0f);
  shape->SetZLayer(Graphic3d_ZLayerId_Top);
  m_targetHighlight = shape;
  m_context->Display(shape, AIS_Shaded, -1, false); // not selectable
}

void PmiScene::applyView(const core::PmiSavedView& view, const OccHandle<V3d_View>& v3dView) {
  if (m_pmi) {
    setOnlyVisible(view.pmi);
  }

  // Camera: orientation from the file, then fit. OCCT's reader keeps only the
  // view volume's projection point, which is relative to the camera's own frame
  // (the camera position itself is dropped), and does not convert the view
  // window to the document unit, so neither can place the camera reliably.
  // See docs/pmi-coverage.md.
  const OccHandle<Graphic3d_Camera>& camera = v3dView->Camera();
  camera->SetProjectionType(view.perspective ? Graphic3d_Camera::Projection_Perspective
                                             : Graphic3d_Camera::Projection_Orthographic);
  camera->SetDirection(view.viewDirection);
  camera->SetUp(view.upDirection);
  camera->OrthogonalizeUp();
  v3dView->FitAll(0.05, false);
  v3dView->ZFitAll();

  // Clipping planes.
  clearViewClipping(v3dView);
  for (const gp_Pln& plane : view.clippingPlanes) {
    OccHandle<Graphic3d_ClipPlane> clip = new Graphic3d_ClipPlane(plane);
    clip->SetCapping(false);
    v3dView->AddClipPlane(clip);
    m_clipPlanes.push_back(clip);
  }
}

void PmiScene::clearViewClipping(const OccHandle<V3d_View>& v3dView) {
  for (const auto& clip : m_clipPlanes) {
    v3dView->RemoveClipPlane(clip);
  }
  m_clipPlanes.clear();
}

int PmiScene::presentationCount() const {
  int count = 0;
  for (const Entry& entry : m_entries) {
    count += static_cast<int>(entry.placements.size());
  }
  return count;
}

} // namespace sc::render
