#include "core/PmiModel.h"

#include "core/OccCompat.h"

#include <TCollection_HAsciiString.hxx>
#include <TDF_Tool.hxx>
#include <XCAFDoc_ClippingPlaneTool.hxx>
#include <XCAFDoc_Datum.hxx>
#include <XCAFDoc_DimTolTool.hxx>
#include <XCAFDoc_Dimension.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_GeomTolerance.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_View.hxx>
#include <XCAFDoc_ViewTool.hxx>
#include <XCAFView_Object.hxx>

#include <algorithm>
#include <map>
#include <tuple>

namespace sc::core {

namespace {

std::string ascii(const OccHandle<TCollection_HAsciiString>& text) {
  return text.IsNull() ? std::string() : std::string(text->ToCString());
}

// The part (prototype shape label) that a target label belongs to: sub-shape
// labels live below their shape's label; component references point to it.
TDF_Label owningPart(TDF_Label label) {
  while (!label.IsNull() && XCAFDoc_ShapeTool::IsSubShape(label)) {
    label = label.Father();
  }
  if (!label.IsNull() && XCAFDoc_ShapeTool::IsReference(label)) {
    TDF_Label referred;
    if (XCAFDoc_ShapeTool::GetReferredShape(label, referred)) {
      return referred;
    }
  }
  return !label.IsNull() && XCAFDoc_ShapeTool::IsShape(label) ? label : TDF_Label();
}

template <class Object>
void readPresentation(const OccHandle<Object>& object, PmiItem& item) {
  item.presentation = object->GetPresentation();
  item.presentationName = ascii(object->GetPresentationName());
  if (object->HasPlane()) {
    item.plane = object->GetPlane();
  }
  if (object->HasPoint()) {
    item.point1 = object->GetPoint();
  }
}

// Called through the tool instance: these are member functions in OCCT 7.x and static in 8.
void readTargets(const OccHandle<XCAFDoc_DimTolTool>& tool, const TDF_Label& label, PmiItem& item) {
  occ::LabelSequence first, second;
  tool->GetRefShapeLabel(label, first, second);
  for (const TDF_Label& target : first) {
    item.targetLabels.push_back(target);
    item.targets.push_back(XCAFDoc_ShapeTool::GetShape(target));
  }
  for (const TDF_Label& target : second) {
    item.secondTargetLabels.push_back(target);
    item.secondTargets.push_back(XCAFDoc_ShapeTool::GetShape(target));
  }
  for (const auto* labels : {&item.targetLabels, &item.secondTargetLabels}) {
    for (const TDF_Label& target : *labels) {
      item.ownerPart = owningPart(target);
      if (!item.ownerPart.IsNull()) {
        return;
      }
    }
  }
}

std::string generatedName(const std::string& typeName, int ordinal) {
  return typeName + " " + std::to_string(ordinal);
}

bool isAncestorOrSelf(const TDF_Label& ancestor, TDF_Label label) {
  for (; !label.IsNull(); label = label.Father()) {
    if (label == ancestor) {
      return true;
    }
  }
  return false;
}

} // namespace

const char* toString(PmiKind kind) {
  switch (kind) {
  case PmiKind::Dimension:
    return "Dimension";
  case PmiKind::GeometricTolerance:
    return "Geometric tolerance";
  case PmiKind::Datum:
    return "Datum";
  case PmiKind::DatumTarget:
    return "Datum target";
  }
  return "PMI";
}

PmiModel::PmiModel(const DocumentPtr& document, int decimals) : m_document(document), m_decimals(decimals) {
  if (!document || document->ocaf().IsNull()) {
    return;
  }
  // Datums first: tolerances refer to them.
  readDatums();
  readDimensions();
  readTolerances();
  readViews();
}

void PmiModel::readDatums() {
  OccHandle<XCAFDoc_DimTolTool> tool = XCAFDoc_DocumentTool::DimTolTool(m_document->ocaf()->Main());
  occ::LabelSequence labels;
  tool->GetDatumLabels(labels);
  std::map<std::string, PmiId> byKey; // merge the per-reference copies of one datum
  int unnamed = 0;
  for (const TDF_Label& label : labels) {
    OccHandle<XCAFDoc_Datum> attribute;
    if (!label.FindAttribute(XCAFDoc_Datum::GetID(), attribute)) {
      continue;
    }
    OccHandle<XCAFDimTolObjects_DatumObject> object = attribute->GetObject();
    if (object.IsNull()) {
      continue;
    }
    PmiItem item;
    item.label = label;
    item.kind = object->IsDatumTarget() ? PmiKind::DatumTarget : PmiKind::Datum;
    item.typeName = item.kind == PmiKind::DatumTarget
                        ? "Datum target (" + pmi::datumTargetTypeName(object->GetDatumTargetType()) + ")"
                        : "Datum";
    item.text = pmi::datumText(object);
    item.properties = pmi::datumProperties(object, m_decimals);
    readPresentation(object, item);
    if (object->HasPointText()) {
      item.textPosition = object->GetPointTextAttach();
    }
    readTargets(tool, label, item);

    const std::string key = item.text.empty() ? "#" + std::to_string(++unnamed)
                                              : std::string(toString(item.kind)) + ":" + item.text;
    const auto existing = byKey.find(key);
    if (existing != byKey.end()) {
      PmiItem& merged = m_items[static_cast<size_t>(existing->second)];
      merged.aliases.push_back(label);
      for (size_t i = 0; i < item.targets.size(); ++i) {
        const bool known = std::find(merged.targetLabels.begin(), merged.targetLabels.end(),
                                     item.targetLabels[i]) != merged.targetLabels.end();
        if (!known) {
          merged.targetLabels.push_back(item.targetLabels[i]);
          merged.targets.push_back(item.targets[i]);
        }
      }
      if (merged.ownerPart.IsNull()) {
        merged.ownerPart = item.ownerPart;
      }
      if (merged.presentation.IsNull() && !item.presentation.IsNull()) {
        merged.presentation = item.presentation;
        merged.presentationName = item.presentationName;
        merged.plane = item.plane;
        merged.textPosition = item.textPosition;
        merged.point1 = item.point1;
      }
      continue;
    }
    item.id = static_cast<PmiId>(m_items.size());
    item.name = !item.text.empty() ? item.typeName + " " + item.text : generatedName(item.typeName, unnamed);
    byKey.emplace(key, item.id);
    m_items.push_back(std::move(item));
  }
  // Datums show their letter; a presentation name from the file is kept as a property.
  for (PmiItem& datum : m_items) {
    if (!datum.presentationName.empty()) {
      datum.properties.emplace_back("Presentation", datum.presentationName);
    }
  }
}

void PmiModel::readDimensions() {
  OccHandle<XCAFDoc_DimTolTool> tool = XCAFDoc_DocumentTool::DimTolTool(m_document->ocaf()->Main());
  occ::LabelSequence labels;
  tool->GetDimensionLabels(labels);
  std::map<std::string, int> ordinals;
  for (const TDF_Label& label : labels) {
    OccHandle<XCAFDoc_Dimension> attribute;
    if (!label.FindAttribute(XCAFDoc_Dimension::GetID(), attribute)) {
      continue;
    }
    OccHandle<XCAFDimTolObjects_DimensionObject> object = attribute->GetObject();
    if (object.IsNull()) {
      continue;
    }
    PmiItem item;
    item.id = static_cast<PmiId>(m_items.size());
    item.label = label;
    item.kind = PmiKind::Dimension;
    item.semantic = object->GetType() != XCAFDimTolObjects_DimensionType_CommonLabel;
    item.typeName = item.semantic ? pmi::dimensionTypeName(object->GetType()) : "Annotation";
    item.text = item.semantic ? pmi::dimensionText(object, m_decimals) : std::string();
    item.properties = pmi::dimensionProperties(object, m_decimals);
    readPresentation(object, item);
    if (object->HasPoint2()) {
      item.point2 = object->GetPoint2();
    }
    if (object->HasTextPoint()) {
      item.textPosition = object->GetPointTextAttach();
    }
    readTargets(tool, label, item);
    item.name = !item.presentationName.empty() ? item.presentationName
                                               : generatedName(item.typeName, ++ordinals[item.typeName]);
    m_items.push_back(std::move(item));
  }
}

void PmiModel::readTolerances() {
  OccHandle<XCAFDoc_DimTolTool> tool = XCAFDoc_DocumentTool::DimTolTool(m_document->ocaf()->Main());
  occ::LabelSequence labels;
  tool->GetGeomToleranceLabels(labels);
  std::map<std::string, int> ordinals;
  for (const TDF_Label& label : labels) {
    OccHandle<XCAFDoc_GeomTolerance> attribute;
    if (!label.FindAttribute(XCAFDoc_GeomTolerance::GetID(), attribute)) {
      continue;
    }
    OccHandle<XCAFDimTolObjects_GeomToleranceObject> object = attribute->GetObject();
    if (object.IsNull()) {
      continue;
    }
    PmiItem item;
    item.id = static_cast<PmiId>(m_items.size());
    item.label = label;
    item.kind = PmiKind::GeometricTolerance;
    item.semantic = object->GetType() != XCAFDimTolObjects_GeomToleranceType_None;

    // Datum references, ordered primary / secondary / tertiary.
    occ::LabelSequence datumLabels;
    tool->GetDatumWithObjectOfTolerLabels(label, datumLabels);
    std::vector<std::tuple<int, PmiId, OccHandle<XCAFDimTolObjects_DatumObject>>> ordered;
    std::vector<OccHandle<XCAFDimTolObjects_DatumObject>> datumObjects;
    for (const TDF_Label& datumLabel : datumLabels) {
      const PmiId datum = itemFor(datumLabel);
      OccHandle<XCAFDoc_Datum> datumAttribute;
      if (datum == kNoPmi || !datumLabel.FindAttribute(XCAFDoc_Datum::GetID(), datumAttribute) ||
          datumAttribute->GetObject().IsNull()) {
        continue;
      }
      ordered.emplace_back(datumAttribute->GetObject()->GetPosition(), datum, datumAttribute->GetObject());
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });
    for (const auto& [position, datum, datumObject] : ordered) {
      (void)position;
      item.datums.push_back(datum);
      datumObjects.push_back(datumObject); // this reference's own object: it carries its modifiers (e.g. B Ⓜ)
    }

    item.typeName = item.semantic ? pmi::toleranceTypeName(object->GetType()) : "Annotation";
    if (item.semantic) {
      item.frame = pmi::featureControlFrame(object, datumObjects, m_decimals);
      item.text = pmi::toleranceText(object, datumObjects, m_decimals);
    }
    item.properties = pmi::toleranceProperties(object, datumObjects, m_decimals);
    readPresentation(object, item);
    if (object->HasPointText()) {
      item.textPosition = object->GetPointTextAttach();
    }
    readTargets(tool, label, item);
    item.name = !item.presentationName.empty() ? item.presentationName
                                               : generatedName(item.typeName, ++ordinals[item.typeName]);
    m_items.push_back(std::move(item));
  }
}

void PmiModel::readViews() {
  const TDF_Label main = m_document->ocaf()->Main();
  if (!XCAFDoc_DocumentTool::CheckViewTool(main)) {
    return;
  }
  OccHandle<XCAFDoc_ViewTool> tool = XCAFDoc_DocumentTool::ViewTool(main);
  OccHandle<XCAFDoc_ClippingPlaneTool> clipping = XCAFDoc_DocumentTool::ClippingPlaneTool(main);
  occ::LabelSequence labels;
  tool->GetViewLabels(labels);
  for (const TDF_Label& label : labels) {
    OccHandle<XCAFDoc_View> attribute;
    if (!label.FindAttribute(XCAFDoc_View::GetID(), attribute)) {
      continue;
    }
    OccHandle<XCAFView_Object> object = attribute->GetObject();
    if (object.IsNull()) {
      continue;
    }
    PmiSavedView view;
    view.id = static_cast<int>(m_views.size());
    view.label = label;
    view.name = ascii(object->Name());
    if (view.name.empty()) {
      view.name = "View " + std::to_string(view.id + 1);
    }
    view.perspective = object->Type() == XCAFView_ProjectionType_Central;
    view.projectionPoint = object->ProjectionPoint();
    view.viewDirection = object->ViewDirection();
    view.upDirection = object->UpDirection();
    view.zoom = object->ZoomFactor();
    view.windowWidth = object->WindowHorizontalSize();
    view.windowHeight = object->WindowVerticalSize();
    if (object->HasFrontPlaneClipping()) {
      view.frontPlaneDistance = object->FrontPlaneDistance();
    }
    if (object->HasBackPlaneClipping()) {
      view.backPlaneDistance = object->BackPlaneDistance();
    }

    occ::LabelSequence refs;
    if (tool->GetRefShapeLabel(label, refs)) {
      for (const TDF_Label& shape : refs) {
        view.shapes.push_back(shape);
      }
    }
    refs.Clear();
    if (tool->GetRefGDTLabel(label, refs)) {
      for (const TDF_Label& gdt : refs) {
        const PmiId id = itemFor(gdt);
        if (id != kNoPmi) {
          view.pmi.push_back(id);
        }
      }
    }
    refs.Clear();
    if (!clipping.IsNull() && tool->GetRefClippingPlaneLabel(label, refs)) {
      for (const TDF_Label& planeLabel : refs) {
        gp_Pln plane;
        OccHandle<TCollection_HAsciiString> name;
        bool capping = false;
        if (clipping->GetClippingPlane(planeLabel, plane, name, capping)) {
          view.clippingPlanes.push_back(plane);
        }
      }
    }
    m_views.push_back(std::move(view));
  }
}

int PmiModel::count(PmiKind kind) const {
  return static_cast<int>(std::count_if(m_items.begin(), m_items.end(),
                                        [kind](const PmiItem& item) { return item.kind == kind; }));
}

int PmiModel::uninterpretedCount() const {
  return static_cast<int>(
      std::count_if(m_items.begin(), m_items.end(), [](const PmiItem& item) { return !item.semantic; }));
}

PmiId PmiModel::itemFor(const TDF_Label& label) const {
  for (const PmiItem& item : m_items) {
    if (item.label == label ||
        std::find(item.aliases.begin(), item.aliases.end(), label) != item.aliases.end()) {
      return item.id;
    }
  }
  return kNoPmi;
}

std::vector<PmiId> PmiModel::itemsTargeting(const TDF_Label& shapeLabel) const {
  std::vector<PmiId> result;
  for (const PmiItem& item : m_items) {
    for (const auto* labels : {&item.targetLabels, &item.secondTargetLabels}) {
      if (std::any_of(labels->begin(), labels->end(), [&shapeLabel](const TDF_Label& target) {
            return isAncestorOrSelf(shapeLabel, target);
          })) {
        result.push_back(item.id);
        break;
      }
    }
  }
  return result;
}

} // namespace sc::core
