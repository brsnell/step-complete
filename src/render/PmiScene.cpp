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
#include <Graphic3d_AspectText3d.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_Group.hxx>
#include <Poly_Polygon3D.hxx>
#include <Poly_Triangulation.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Prs3d_Text.hxx>
#include <Prs3d_TextAspect.hxx>
#include <Select3D_SensitivePoint.hxx>
#include <Select3D_SensitiveSegment.hxx>
#include <Select3D_SensitiveTriangle.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <SelectMgr_Selection.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>

#include <algorithm>

namespace sc::render {

namespace {

// Annotations: dark blue, readable on the light background and on part colors.
const Quantity_Color kPmiColor(0.05, 0.20, 0.55, Quantity_TOC_sRGB);
const Quantity_Color kLabelBackground(1.0, 1.0, 0.94, Quantity_TOC_sRGB);
// Geometry a selected annotation applies to.
const Quantity_Color kTargetColor(1.0, 0.62, 0.10, Quantity_TOC_sRGB);

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
  if (isGraphical() || m_label.empty()) {
    return;
  }

  // Semantic-only: leader line plus a boxed screen-space label.
  if (m_leaderTarget) {
    OccHandle<Graphic3d_Group> leader = prs->NewGroup();
    leader->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(kPmiColor, Aspect_TOL_SOLID, 1.0 * m_dpr));
    OccHandle<Graphic3d_ArrayOfSegments> segment = new Graphic3d_ArrayOfSegments(2);
    segment->AddVertex(m_labelAnchor);
    segment->AddVertex(*m_leaderTarget);
    leader->AddPrimitiveArray(segment);
  }
  OccHandle<Graphic3d_Group> label = prs->NewGroup();
  OccHandle<Prs3d_TextAspect> aspect = new Prs3d_TextAspect();
  aspect->SetColor(kPmiColor);
  aspect->SetHeight(13.0 * m_dpr);
  aspect->SetHorizontalJustification(Graphic3d_HTA_CENTER);
  aspect->SetVerticalJustification(Graphic3d_VTA_CENTER);
  aspect->Aspect()->SetDisplayType(Aspect_TODT_SUBTITLE);
  aspect->Aspect()->SetColorSubTitle(kLabelBackground);
  label->SetGroupPrimitivesAspect(aspect->Aspect());
  Prs3d_Text::Draw(label, aspect, TCollection_ExtendedString(m_label.c_str(), true), m_labelAnchor);
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
  if (!isGraphical() && !m_label.empty()) {
    OccHandle<Select3D_SensitivePoint> anchor = new Select3D_SensitivePoint(owner, m_labelAnchor);
    anchor->SetSensitivityFactor(static_cast<int>(12 * m_dpr)); // the label box, roughly
    selection->Add(anchor);
    if (m_leaderTarget) {
      selection->Add(new Select3D_SensitiveSegment(owner, m_labelAnchor, *m_leaderTarget));
    }
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
  for (const core::PmiItem& item : m_pmi->items()) {
    Entry& entry = m_entries[static_cast<size_t>(item.id)];
    for (const TopLoc_Location& placement : placementsOf(item)) {
      OccHandle<PmiPresentation> prs = new PmiPresentation(item, placement, devicePixelRatio);
      m_objectToPmi[prs.get()] = item.id;
      m_context->Display(prs, 0, 0, false);
      entry.placements.push_back(prs);
    }
  }
}

void PmiScene::clear() {
  removeTargetHighlight();
  for (Entry& entry : m_entries) {
    for (const auto& prs : entry.placements) {
      m_context->Remove(prs, false);
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
    for (const auto& prs : m_entries[static_cast<size_t>(id)].placements) {
      if (m_context->IsDisplayed(prs)) {
        m_context->AddOrRemoveSelected(prs, false);
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

  // Camera. The projection point is the eye for perspective views and the view
  // center for parallel ones; the view window gives the visible extent.
  const OccHandle<Graphic3d_Camera>& camera = v3dView->Camera();
  const Bnd_Box sceneBox = v3dView->View()->MinMaxValues();
  const double distance = sceneBox.IsVoid() ? 1000.0 : std::max(std::sqrt(sceneBox.SquareExtent()), 1.0);

  camera->SetProjectionType(view.perspective ? Graphic3d_Camera::Projection_Perspective
                                             : Graphic3d_Camera::Projection_Orthographic);
  const gp_Vec direction(view.viewDirection);
  if (view.perspective) {
    camera->SetEyeAndCenter(view.projectionPoint, view.projectionPoint.Translated(direction * distance));
  } else {
    camera->SetEyeAndCenter(view.projectionPoint.Translated(-direction * distance), view.projectionPoint);
  }
  camera->SetUp(view.upDirection);
  camera->OrthogonalizeUp();
  if (!view.perspective && view.windowHeight > 0.0) {
    camera->SetScale(view.windowHeight);
  } else {
    v3dView->FitAll(0.05, false);
  }
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
