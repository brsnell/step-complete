#pragma once

#include "core/OccHandle.h"
#include "render/OcctViewport.h"

#include <AIS_InteractiveObject.hxx>
#include <Bnd_Box.hxx>
#include <Graphic3d_ClipPlane.hxx>
#include <Graphic3d_SequenceOfHClipPlane.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>

#include <QObject>

#include <array>

namespace sc::render {

class SceneManager;
class SectionHandle;

// One section plane: the part on the side the normal points to is cut away.
struct SectionPlane {
  bool enabled = false;
  gp_Dir normal{0, 0, 1};
  double offset = 0.0; // signed distance of the plane from the origin along the normal (mm)
  bool capping = true; // fill the cut solids
  bool hatch = true;   // hatch the caps
  gp_Pln plane() const;
};

// Section view: up to three clipping planes applied to the parts (PMI,
// measurement witnesses and the view cube stay whole). Cut solids are capped
// with the part's own color, optionally hatched. Each active plane shows a
// translucent handle in the view that can be dragged along its normal.
class SectionTool : public QObject, public DragHandler {
  Q_OBJECT

public:
  static constexpr int kMaxPlanes = 3;

  SectionTool(OcctViewport* viewport, SceneManager* scene, QObject* parent = nullptr);
  ~SectionTool() override;

  // Model extent: sizes the handles and the offset range. Call after loading a model.
  void setBounds(const Bnd_Box& bounds);
  const Bnd_Box& bounds() const { return m_bounds; }

  void setPlane(int index, const SectionPlane& plane);
  const SectionPlane& plane(int index) const { return m_planes.at(static_cast<size_t>(index)); }
  // Offsets that cross the model along the plane's normal (min, max).
  std::pair<double, double> offsetRange(int index) const;
  // Reverses the normal, keeping the plane where it is (the other side is cut).
  void flip(int index);
  bool isActive() const;
  // Disables all planes and removes clipping.
  void reset();

  void setHandlesVisible(bool visible);
  bool handlesVisible() const { return m_handlesVisible; }

  // DragHandler: dragging a handle moves its plane along the normal.
  bool beginDrag(const OccHandle<AIS_InteractiveObject>& detected,
                 const NCollection_Vec2<int>& pixel) override;
  void drag(const NCollection_Vec2<int>& from, const NCollection_Vec2<int>& to) override;
  void endDrag() override;

Q_SIGNALS:
  void planeChanged(int index);

private:
  void apply(int index);
  void updateClipSequence();
  void updateHandle(int index);
  // Model units per pixel of mouse movement along the plane's normal on screen.
  double offsetPerPixel(int index, NCollection_Vec2<double>& screenDirection) const;

  OcctViewport* m_viewport;
  SceneManager* m_scene;
  Bnd_Box m_bounds;
  std::array<SectionPlane, kMaxPlanes> m_planes;
  std::array<OccHandle<Graphic3d_ClipPlane>, kMaxPlanes> m_clip;
  std::array<OccHandle<SectionHandle>, kMaxPlanes> m_handles;
  OccHandle<Graphic3d_SequenceOfHClipPlane> m_sequence;
  bool m_handlesVisible = true;

  int m_dragIndex = -1;
  double m_dragStartOffset = 0.0;
};

} // namespace sc::render
