#pragma once

#include "core/AssemblyIndex.h"
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
#include <vector>

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
  static constexpr int MaxPlanes = 3;

  SectionTool(OcctViewport* viewport, SceneManager* scene, QObject* parent = nullptr);
  ~SectionTool() override;

  // Model extent: sizes the handles and the offset range. Call after loading a model.
  void setBounds(const Bnd_Box& bounds);
  const Bnd_Box& bounds() const { return _bounds; }

  void setPlane(int index, const SectionPlane& plane);
  const SectionPlane& plane(int index) const { return _planes.at(static_cast<size_t>(index)); }
  // Offsets that cross the model along the plane's normal (min, max).
  std::pair<double, double> offsetRange(int index) const;
  // Reverses the normal, keeping the plane where it is (the other side is cut).
  void flip(int index);
  bool isActive() const;
  // Disables all planes and removes clipping.
  void reset();

  void setHandlesVisible(bool visible);
  bool handlesVisible() const { return _handlesVisible; }

  // --- Plane on a picked face (docs/designs/angled-section-planes.md) ---------------
  // Makes faces pickable; the next click on a flat face puts plane 'index' on it (enabled,
  // through the face, normal pointing out of the material). pickStarted is emitted before faces
  // become pickable, so a caller can stop other tools that set selection modes (the measure
  // tool). Calling it while picking switches to another plane. The selection from before the
  // pick comes back when it ends.
  void pickFace(int index);
  // Ends a pick without changing the plane. No-op when not picking.
  void cancelPick();
  bool isPicking() const { return _pickIndex >= 0; }
  int pickingPlane() const { return _pickIndex; }

  // DragHandler: dragging a handle moves its plane along the normal.
  bool beginDrag(const OccHandle<AIS_InteractiveObject>& detected,
                 const NCollection_Vec2<int>& pixel) override;
  void drag(const NCollection_Vec2<int>& from, const NCollection_Vec2<int>& to) override;
  void endDrag() override;

public Q_SLOTS:
  // Connected to OcctViewport::selectionChanged; public for tests.
  void onSelectionChanged();

Q_SIGNALS:
  void planeChanged(int index);
  void pickStarted(int index);
  // The click was not on a flat face; the pick goes on.
  void pickRejected();
  // The pick ended: 'picked' when a face set the plane, false when cancelled.
  void pickEnded(int index, bool picked);

private:
  void apply(int index);
  void updateClipSequence();
  void updateHandle(int index);
  // Restores the selection modes and the selection; 'restoreSelection' false after a model change.
  void endPick(bool picked, bool restoreSelection = true);
  // Model units per pixel of mouse movement along the plane's normal on screen.
  double offsetPerPixel(int index, NCollection_Vec2<double>& screenDirection) const;

  OcctViewport* _viewport;
  SceneManager* _scene;
  Bnd_Box _bounds;
  std::array<SectionPlane, MaxPlanes> _planes;
  std::array<OccHandle<Graphic3d_ClipPlane>, MaxPlanes> _clip;
  std::array<OccHandle<SectionHandle>, MaxPlanes> _handles;
  OccHandle<Graphic3d_SequenceOfHClipPlane> _sequence;
  bool _handlesVisible = true;

  int _pickIndex = -1;
  std::vector<core::NodeId> _selectionBeforePick;
  bool _updatingSelection = false;

  int _dragIndex = -1;
  double _dragStartOffset = 0.0;
};

} // namespace sc::render
