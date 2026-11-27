#include "render/MarkupScene.h"

#include "render/PmiFont.h"
#include "render/SceneManager.h"

#include <AIS_InteractiveContext.hxx>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_Text.hxx>
#include <Prs3d_TextAspect.hxx>
#include <Quantity_Color.hxx>
#include <SelectMgr_Selection.hxx>
#include <StdSelect_ViewerSelector3d.hxx>
#include <TCollection_ExtendedString.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <utility>

namespace sc::render {

namespace {

const Quantity_Color NotePaper(1.0, 0.95, 0.60, Quantity_TOC_sRGB);
const Quantity_Color NoteInk(0.16, 0.12, 0.02, Quantity_TOC_sRGB);
const Quantity_Color NoteBorder(0.62, 0.45, 0.02, Quantity_TOC_sRGB);
// Magenta rather than red: red is a common part color (the fixture's base plate is red).
const Quantity_Color StrokeColor(0.92, 0.10, 0.62, Quantity_TOC_sRGB);
const Quantity_Color EmphasisColor(1.0, 0.50, 0.0, Quantity_TOC_sRGB);

// Sizes in logical pixels (scaled by the device pixel ratio).
constexpr double NoteFontSize = 13.0;
constexpr double NoteLineHeight = 17.0;
constexpr double NotePadding = 6.0;
constexpr double NoteOffset = 24.0; // the box's lower-left corner, up and right of the point
constexpr double DotRadius = 3.5;
constexpr double StrokeWidth = 3.0;
constexpr double MinStrokeStep = 2.0; // pixels between kept stroke points
constexpr double Pi = 3.14159265358979323846;

std::vector<std::string> linesOf(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }
  if (lines.empty()) {
    lines.emplace_back();
  }
  return lines;
}

// A note: a dot at the point, a leader and a box holding the text, in device pixels at a
// constant size next to the point (x right, y up, origin at the point).
class MarkupNoteLabel : public AIS_InteractiveObject {
  DEFINE_STANDARD_RTTI_INLINE(MarkupNoteLabel, AIS_InteractiveObject)

public:
  MarkupNoteLabel(const std::string& text, const gp_Pnt& anchor, bool emphasized, double dpr)
      : _lines(linesOf(text)), _emphasized(emphasized), _dpr(dpr) {
    setAnchor(anchor);
    SetZLayer(Graphic3d_ZLayerId_Topmost);
    double width = 0.0;
    for (const std::string& line : _lines) {
      width = std::max(width, pmiTextWidth(line, NoteFontSize * _dpr));
    }
    _width = std::ceil(width) + 2.0 * NotePadding * _dpr;
    _height = static_cast<double>(_lines.size()) * NoteLineHeight * _dpr + 2.0 * NotePadding * _dpr;
  }

  // The point the note is drawn at; the caller redisplays.
  void setAnchor(const gp_Pnt& anchor) {
    SetTransformPersistence(new Graphic3d_TransformPers(Graphic3d_TMF_ZoomRotatePers, anchor));
  }

  bool AcceptDisplayMode(const int mode) const override { return mode == 0; }

protected:
  void Compute(const OccHandle<PrsMgr_PresentationManager>&, const OccHandle<Prs3d_Presentation>& prs,
               const int mode) override {
    if (mode != 0) {
      return;
    }
    const double left = NoteOffset * _dpr;
    const double bottom = NoteOffset * _dpr;
    const double right = left + _width;
    const double top = bottom + _height;

    OccHandle<Graphic3d_AspectFillArea3d> paper = new Graphic3d_AspectFillArea3d();
    paper->SetInteriorStyle(Aspect_IS_SOLID);
    paper->SetInteriorColor(NotePaper);
    paper->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
    paper->SetFaceCulling(Graphic3d_TypeOfBackfacingModel_DoubleSided);
    OccHandle<Graphic3d_Group> fillGroup = prs->NewGroup();
    fillGroup->SetGroupPrimitivesAspect(paper);
    OccHandle<Graphic3d_ArrayOfTriangles> quad = new Graphic3d_ArrayOfTriangles(4, 6);
    quad->AddVertex(gp_Pnt(left, bottom, 0.0));
    quad->AddVertex(gp_Pnt(right, bottom, 0.0));
    quad->AddVertex(gp_Pnt(right, top, 0.0));
    quad->AddVertex(gp_Pnt(left, top, 0.0));
    quad->AddEdges(1, 2, 3);
    quad->AddEdges(1, 3, 4);
    fillGroup->AddPrimitiveArray(quad);

    // The dot at the point.
    const Quantity_Color& lineColor = _emphasized ? EmphasisColor : NoteBorder;
    OccHandle<Graphic3d_AspectFillArea3d> dotAspect = new Graphic3d_AspectFillArea3d();
    dotAspect->SetInteriorStyle(Aspect_IS_SOLID);
    dotAspect->SetInteriorColor(lineColor);
    dotAspect->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
    dotAspect->SetFaceCulling(Graphic3d_TypeOfBackfacingModel_DoubleSided);
    OccHandle<Graphic3d_Group> dotGroup = prs->NewGroup();
    dotGroup->SetGroupPrimitivesAspect(dotAspect);
    constexpr int Sides = 12;
    const double radius = DotRadius * _dpr;
    OccHandle<Graphic3d_ArrayOfTriangles> dot = new Graphic3d_ArrayOfTriangles(Sides + 1, 3 * Sides);
    dot->AddVertex(gp_Pnt(0.0, 0.0, 0.0));
    for (int i = 0; i < Sides; ++i) {
      const double angle = 2.0 * Pi * i / Sides;
      dot->AddVertex(gp_Pnt(radius * std::cos(angle), radius * std::sin(angle), 0.0));
    }
    for (int i = 0; i < Sides; ++i) {
      dot->AddEdges(1, 2 + i, 2 + (i + 1) % Sides);
    }
    dotGroup->AddPrimitiveArray(dot);

    // Leader and border.
    OccHandle<Graphic3d_Group> lineGroup = prs->NewGroup();
    lineGroup->SetGroupPrimitivesAspect(
        new Graphic3d_AspectLine3d(lineColor, Aspect_TOL_SOLID, (_emphasized ? 2.0 : 1.0) * _dpr));
    OccHandle<Graphic3d_ArrayOfSegments> lines = new Graphic3d_ArrayOfSegments(10);
    auto segment = [&lines](double x0, double y0, double x1, double y1) {
      lines->AddVertex(gp_Pnt(x0, y0, 0.0));
      lines->AddVertex(gp_Pnt(x1, y1, 0.0));
    };
    segment(0.0, 0.0, left, bottom);
    segment(left, bottom, right, bottom);
    segment(right, bottom, right, top);
    segment(right, top, left, top);
    segment(left, top, left, bottom);
    lineGroup->AddPrimitiveArray(lines);

    OccHandle<Graphic3d_Group> textGroup = prs->NewGroup();
    OccHandle<Prs3d_TextAspect> aspect = new Prs3d_TextAspect();
    aspect->SetFont(pmiFontName());
    aspect->SetColor(NoteInk);
    aspect->SetHeight(NoteFontSize * _dpr);
    aspect->SetHorizontalJustification(Graphic3d_HTA_LEFT);
    aspect->SetVerticalJustification(Graphic3d_VTA_CENTER);
    textGroup->SetGroupPrimitivesAspect(aspect->Aspect());
    const double lineHeight = NoteLineHeight * _dpr;
    for (size_t i = 0; i < _lines.size(); ++i) {
      if (_lines[i].empty()) {
        continue;
      }
      const double y = top - NotePadding * _dpr - (static_cast<double>(i) + 0.5) * lineHeight;
      Prs3d_Text::Draw(textGroup, aspect, TCollection_ExtendedString(_lines[i].c_str(), true),
                       gp_Pnt(left + NotePadding * _dpr, y, 0.0));
    }
  }

  void ComputeSelection(const OccHandle<SelectMgr_Selection>&, const int) override {}

private:
  std::vector<std::string> _lines;
  bool _emphasized;
  double _dpr;
  double _width = 0.0;
  double _height = 0.0;
};

// A stroke: a polyline in world coordinates.
class MarkupStrokePresentation : public AIS_InteractiveObject {
  DEFINE_STANDARD_RTTI_INLINE(MarkupStrokePresentation, AIS_InteractiveObject)

public:
  MarkupStrokePresentation(std::vector<gp_Pnt> points, bool emphasized, double dpr)
      : _points(std::move(points)), _emphasized(emphasized), _dpr(dpr) {
    SetZLayer(Graphic3d_ZLayerId_Topmost);
  }

  void setPoints(std::vector<gp_Pnt> points) { _points = std::move(points); }

  bool AcceptDisplayMode(const int mode) const override { return mode == 0; }

protected:
  void Compute(const OccHandle<PrsMgr_PresentationManager>&, const OccHandle<Prs3d_Presentation>& prs,
               const int mode) override {
    if (mode != 0 || _points.size() < 2) {
      return;
    }
    OccHandle<Graphic3d_Group> group = prs->NewGroup();
    group->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(_emphasized ? EmphasisColor : StrokeColor,
                                                               Aspect_TOL_SOLID, StrokeWidth * _dpr));
    OccHandle<Graphic3d_ArrayOfPolylines> line =
        new Graphic3d_ArrayOfPolylines(static_cast<int>(_points.size()));
    for (const gp_Pnt& point : _points) {
      line->AddVertex(point);
    }
    group->AddPrimitiveArray(line);
  }

  void ComputeSelection(const OccHandle<SelectMgr_Selection>&, const int) override {}

private:
  std::vector<gp_Pnt> _points;
  bool _emphasized;
  double _dpr;
};

} // namespace

MarkupScene::MarkupScene(OcctViewport* viewport, SceneManager* scene, QObject* parent)
    : QObject(parent), _viewport(viewport), _scene(scene) {
  connect(_viewport, &OcctViewport::selectionChanged, this, &MarkupScene::onSelectionChanged);
}

MarkupScene::~MarkupScene() {
  if (_viewport->pressHandler() == this) {
    _viewport->setPressHandler(nullptr);
  }
  // The context may already be gone when the viewport is destroyed first.
  if (!_viewport->context().IsNull()) {
    clear();
  }
}

double MarkupScene::devicePixelRatio() const {
  return std::max(1.0, _viewport->devicePixelRatioF());
}

void MarkupScene::setMarkup(const core::Markup& markup) {
  _markup = markup;
  rebuild();
}

void MarkupScene::clear() {
  _markup.clear();
  _emphasized = 0;
  _offsets.clear();
  cancelNotePick();
  setSketching(false);
  rebuild();
}

void MarkupScene::refresh() {
  rebuild();
}

void MarkupScene::setPartOffsets(std::vector<gp_Vec> offsets) {
  _offsets = std::move(offsets);
  applyOffsets();
}

void MarkupScene::applyOffsets() {
  const OccHandle<AIS_InteractiveContext>& context = _viewport->context();
  for (Item& item : _items) {
    gp_Vec offset(0.0, 0.0, 0.0);
    if (item.node != core::NoNode && static_cast<size_t>(item.node) < _offsets.size()) {
      offset = _offsets[static_cast<size_t>(item.node)];
    }
    if (offset.IsEqual(item.offset, 0.0, 0.0)) {
      continue;
    }
    item.offset = offset;
    gp_Trsf translation;
    translation.SetTranslation(offset);
    item.box = item.assembledBox.Transformed(translation);
    if (auto* note = dynamic_cast<MarkupNoteLabel*>(item.object.get()); note != nullptr && item.anchor) {
      note->setAnchor(item.anchor->Translated(offset)); // drawn in screen space at its anchor
      if (context->IsDisplayed(item.object)) {
        context->Redisplay(item.object, false);
      }
    } else if (context->IsDisplayed(item.object)) {
      context->SetLocation(item.object, TopLoc_Location(translation));
    } else {
      item.object->SetLocalTransformation(translation);
    }
  }
}

void MarkupScene::rebuild() {
  const OccHandle<AIS_InteractiveContext>& context = _viewport->context();
  for (const Item& item : _items) {
    context->Remove(item.object, false);
  }
  _items.clear();
  _missing.clear();
  const std::shared_ptr<const core::AssemblyIndex>& index = _scene->model();
  if (!index) {
    return;
  }
  const double dpr = devicePixelRatio();
  const auto nodeOf = [&index](const std::string& part) {
    return part.empty() ? core::NoNode : index->nodeForPathId(part);
  };
  for (const core::MarkupNote& note : _markup.notes()) {
    const std::optional<gp_Trsf> placement = core::markupPlacement(*index, note.part);
    if (!placement) {
      _missing.push_back(note.id);
      continue;
    }
    const gp_Pnt world = note.point.Transformed(*placement);
    Item item;
    item.id = note.id;
    item.part = note.part;
    item.object = new MarkupNoteLabel(note.text, world, note.id == _emphasized, dpr);
    item.box.Add(world);
    item.node = nodeOf(note.part);
    item.anchor = world;
    item.assembledBox = item.box;
    _items.push_back(std::move(item));
  }
  for (const core::MarkupStroke& stroke : _markup.strokes()) {
    const std::optional<gp_Trsf> placement = core::markupPlacement(*index, stroke.part);
    if (!placement) {
      _missing.push_back(stroke.id);
      continue;
    }
    std::vector<gp_Pnt> world;
    world.reserve(stroke.points.size());
    Bnd_Box box;
    for (const gp_Pnt& point : stroke.points) {
      world.push_back(point.Transformed(*placement));
      box.Add(world.back());
    }
    Item item;
    item.id = stroke.id;
    item.part = stroke.part;
    item.object = new MarkupStrokePresentation(std::move(world), stroke.id == _emphasized, dpr);
    item.box = box;
    item.node = nodeOf(stroke.part);
    item.assembledBox = box;
    _items.push_back(std::move(item));
  }
  applyOffsets(); // before display: a new item takes its part's offset at once
  for (Item& item : _items) {
    display(item);
  }
}

bool MarkupScene::partShown(const std::string& part) const {
  if (part.empty()) {
    return true;
  }
  const std::shared_ptr<const core::AssemblyIndex>& index = _scene->model();
  const core::NodeId node = index ? index->nodeForPathId(part) : core::NoNode;
  return node != core::NoNode && _scene->isVisible(node);
}

void MarkupScene::display(Item& item) {
  const OccHandle<AIS_InteractiveContext>& context = _viewport->context();
  const bool wanted = _shown && partShown(item.part);
  const bool displayed = context->IsDisplayed(item.object);
  if (wanted && !displayed) {
    context->Display(item.object, 0, -1, false); // not selectable
  } else if (!wanted && displayed) {
    context->Erase(item.object, false);
  }
}

void MarkupScene::setShown(bool shown) {
  _shown = shown;
  updateVisibility();
}

void MarkupScene::updateVisibility() {
  for (Item& item : _items) {
    display(item);
  }
}

void MarkupScene::setEmphasized(int id) {
  if (id == _emphasized) {
    return;
  }
  _emphasized = id;
  rebuild();
}

bool MarkupScene::isDisplayed(int id) const {
  const auto it =
      std::find_if(_items.begin(), _items.end(), [id](const Item& item) { return item.id == id; });
  return it != _items.end() && _viewport->context()->IsDisplayed(it->object);
}

bool MarkupScene::isMissing(int id) const {
  return std::find(_missing.begin(), _missing.end(), id) != _missing.end();
}

int MarkupScene::displayedCount() const {
  return static_cast<int>(std::count_if(_items.begin(), _items.end(), [this](const Item& item) {
    return _viewport->context()->IsDisplayed(item.object);
  }));
}

Bnd_Box MarkupScene::worldBox(int id) const {
  const auto it =
      std::find_if(_items.begin(), _items.end(), [id](const Item& item) { return item.id == id; });
  return it != _items.end() ? it->box : Bnd_Box();
}

// --- Placing a note ----------------------------------------------------------------------

void MarkupScene::startNotePick() {
  if (!_scene->model()) {
    return;
  }
  if (!_picking) {
    // Before the selection modes: the caller may stop the measure tool, which sets modes too.
    Q_EMIT notePickStarted();
  }
  _picking = true;
  _updatingSelection = true;
  _scene->setSelectionModes(SceneManager::subShapeModes(false, false, true));
  _viewport->setAccumulatingSelection(false);
  _updatingSelection = false;
  _viewport->invalidateScene();
}

void MarkupScene::cancelNotePick() {
  if (_picking) {
    endNotePick(std::nullopt);
  }
}

void MarkupScene::endNotePick(const std::optional<core::MarkupNote>& picked) {
  _picking = false;
  _updatingSelection = true;
  _scene->setSelectionModes({});
  _updatingSelection = false;
  _viewport->invalidateScene();
  if (picked) {
    Q_EMIT notePicked(*picked); // whole-part picking is back: the receiver may ask for the text
  }
  Q_EMIT notePickEnded(picked.has_value());
}

void MarkupScene::onSelectionChanged() {
  if (!_picking || _updatingSelection) {
    return;
  }
  const OccHandle<AIS_InteractiveContext>& context = _viewport->context();
  context->InitSelected();
  if (!context->MoreSelected()) {
    return; // a click on empty space: keep waiting
  }
  const std::optional<SceneManager::PickedEntity> entity = _scene->pickedEntity(context->SelectedOwner());
  const std::shared_ptr<const core::AssemblyIndex>& index = _scene->model();
  if (!entity || entity->part == core::NoNode || !index || context->MainSelector()->NbPicked() == 0) {
    return;
  }
  core::MarkupNote note;
  note.part = index->pathId(entity->part);
  const gp_Pnt world = SceneManager::pickedPoint(context, context->SelectedOwner());
  const std::optional<gp_Trsf> placement = core::markupPlacement(*index, note.part);
  note.point = placement ? world.Transformed(placement->Inverted()) : world;
  _updatingSelection = true;
  context->ClearSelected(false);
  _updatingSelection = false;
  endNotePick(note);
}

// --- Sketching -----------------------------------------------------------------------------

void MarkupScene::setSketching(bool on) {
  if (on == _sketching) {
    return;
  }
  _sketching = on;
  if (on) {
    _viewport->setPressHandler(this);
  } else {
    if (_viewport->pressHandler() == this) {
      _viewport->setPressHandler(nullptr);
    }
    _drawing = false;
    _livePoints.clear();
    showLiveStroke();
  }
}

std::optional<gp_Pnt> MarkupScene::onSketchPlane(const NCollection_Vec2<int>& pixel) const {
  const OccHandle<V3d_View>& view = _viewport->view();
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double dx = 0.0;
  double dy = 0.0;
  double dz = 0.0;
  view->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);
  const gp_Vec direction(dx, dy, dz);
  const gp_Vec normal(_sketchPlane.Axis().Direction());
  const double along = direction.Dot(normal);
  if (direction.Magnitude() < 1e-12 || std::abs(along) < 1e-12) {
    return std::nullopt;
  }
  const gp_Pnt start(x, y, z);
  const double t = gp_Vec(start, _sketchPlane.Location()).Dot(normal) / along;
  return start.Translated(direction * t);
}

bool MarkupScene::beginDrag(const OccHandle<AIS_InteractiveObject>& detected,
                            const NCollection_Vec2<int>& pixel) {
  if (!_sketching || !_scene->model()) {
    return false;
  }
  const OccHandle<AIS_InteractiveContext>& context = _viewport->context();
  const OccHandle<Graphic3d_Camera>& camera = _viewport->view()->Camera();
  const core::NodeId part = detected.IsNull() ? core::NoNode : _scene->partOf(detected);
  gp_Pnt origin = camera->Center();
  _sketchPart.clear();
  if (part != core::NoNode && context->MainSelector()->NbPicked() > 0) {
    origin = context->MainSelector()->PickedPoint(1);
    _sketchPart = _scene->model()->pathId(part);
  }
  _sketchPlane = gp_Pln(origin, camera->Direction());
  _livePoints.clear();
  _drawing = true;
  _lastPixel = pixel;
  if (const std::optional<gp_Pnt> point = onSketchPlane(pixel)) {
    _livePoints.push_back(*point);
  }
  return true;
}

void MarkupScene::drag(const NCollection_Vec2<int>&, const NCollection_Vec2<int>& to) {
  if (!_drawing) {
    return;
  }
  const double step = MinStrokeStep * devicePixelRatio();
  const double dx = to.x() - _lastPixel.x();
  const double dy = to.y() - _lastPixel.y();
  if (!_livePoints.empty() && dx * dx + dy * dy < step * step) {
    return;
  }
  if (const std::optional<gp_Pnt> point = onSketchPlane(to)) {
    _livePoints.push_back(*point);
    _lastPixel = to;
    showLiveStroke();
  }
}

void MarkupScene::endDrag() {
  if (!_drawing) {
    return;
  }
  _drawing = false;
  std::vector<gp_Pnt> points = std::exchange(_livePoints, {});
  showLiveStroke();
  if (points.size() < 2 || !_scene->model()) {
    return;
  }
  core::MarkupStroke stroke;
  stroke.part = _sketchPart;
  const std::optional<gp_Trsf> placement = core::markupPlacement(*_scene->model(), stroke.part);
  const gp_Trsf toPart = placement ? placement->Inverted() : gp_Trsf();
  for (const gp_Pnt& point : points) {
    stroke.points.push_back(point.Transformed(toPart));
  }
  Q_EMIT strokeDrawn(stroke);
}

void MarkupScene::showLiveStroke() {
  const OccHandle<AIS_InteractiveContext>& context = _viewport->context();
  if (_livePoints.size() < 2) {
    if (!_liveStroke.IsNull()) {
      context->Remove(_liveStroke, false);
      _liveStroke.Nullify();
      _viewport->invalidateScene();
    }
    return;
  }
  if (_liveStroke.IsNull()) {
    _liveStroke = new MarkupStrokePresentation(_livePoints, false, devicePixelRatio());
    context->Display(_liveStroke, 0, -1, false);
  } else {
    static_cast<MarkupStrokePresentation*>(_liveStroke.get())->setPoints(_livePoints);
    context->Redisplay(_liveStroke, false);
  }
  _viewport->invalidateScene();
}

} // namespace sc::render
