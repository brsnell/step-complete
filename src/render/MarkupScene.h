#pragma once

#include "core/Markup.h"
#include "core/OccHandle.h"
#include "render/OcctViewport.h"

#include <AIS_InteractiveObject.hxx>
#include <Bnd_Box.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec.hxx>

#include <QObject>

#include <map>
#include <optional>
#include <vector>

namespace sc::render {

class SceneManager;

// Shows a model's review markup (docs/designs/markup.md) and takes the input that creates it.
//
// Notes are drawn in screen space at a constant pixel size next to their point, strokes as
// polylines; both stay on top of the model, are not cut by section planes and are not
// selectable. An item on a hidden part is hidden with it, and an item whose part the model does
// not have is not drawn at all (isMissing).
//
// Input: startNotePick() makes faces pickable until one is clicked (notePicked) or the pick is
// cancelled; setSketching(true) takes every plain left drag in the view as a stroke
// (strokeDrawn). Neither changes the markup itself: the caller adds the result and calls
// setMarkup again. The caller redraws the viewport after the setters.
class MarkupScene : public QObject, public DragHandler {
  Q_OBJECT

public:
  MarkupScene(OcctViewport* viewport, SceneManager* scene, QObject* parent = nullptr);
  ~MarkupScene() override;

  // Shows exactly this markup, placed in the scene's current model.
  void setMarkup(const core::Markup& markup);
  void clear();
  const core::Markup& markup() const { return _markup; }

  // Show Markup: off hides every item (they stay listed).
  void setShown(bool shown);
  bool isShown() const { return _shown; }
  // Hides the items of hidden parts and shows the others; call after visibility changes.
  void updateVisibility();
  // Rebuilds the presentations (e.g. the device pixel ratio changed).
  void refresh();
  // Exploded view (docs/designs/exploded-view.md): a world translation per assembly node id;
  // an item on a part moves with it, an item in the world stays. Empty puts them back.
  void setPartOffsets(std::vector<gp_Vec> offsets);

  // One item drawn in the emphasis color (0: none).
  void setEmphasized(int id);
  int emphasized() const { return _emphasized; }

  bool isDisplayed(int id) const; // drawn now
  bool isMissing(int id) const;   // anchored to a part the model does not have
  int displayedCount() const;
  // Where the item is in the world (void for a missing one).
  Bnd_Box worldBox(int id) const;

  // --- Placing a note --------------------------------------------------------------
  void startNotePick();
  void cancelNotePick();
  bool isPickingNote() const { return _picking; }

  // --- Sketching ---------------------------------------------------------------------
  void setSketching(bool on);
  bool isSketching() const { return _sketching; }

  // DragHandler (the viewport's press handler while sketching).
  bool beginDrag(const OccHandle<AIS_InteractiveObject>& detected,
                 const NCollection_Vec2<int>& pixel) override;
  void drag(const NCollection_Vec2<int>& from, const NCollection_Vec2<int>& to) override;
  void endDrag() override;

public Q_SLOTS:
  // Connected to OcctViewport::selectionChanged; public for tests.
  void onSelectionChanged();

Q_SIGNALS:
  void notePickStarted();
  // picked: a face was clicked (notePicked came first); false: cancelled.
  void notePickEnded(bool picked);
  // The clicked point, in the part's coordinates; the text is empty.
  void notePicked(const sc::core::MarkupNote& note);
  // A finished stroke of at least two points, in its part's coordinates.
  void strokeDrawn(const sc::core::MarkupStroke& stroke);

private:
  struct Item {
    int id = 0;
    std::string part; // empty: world
    OccHandle<AIS_InteractiveObject> object;
    Bnd_Box box;
    core::NodeId node = core::NoNode; // the part's node, NoNode in the world
    std::optional<gp_Pnt> anchor;     // a note's point, assembled
    Bnd_Box assembledBox;             // where it is with the model assembled
    gp_Vec offset;                    // the exploded offset applied now
  };
  void rebuild();
  // Moves the items by their part's exploded offset, without rebuilding them.
  void applyOffsets();
  void display(Item& item);
  bool partShown(const std::string& part) const;
  void endNotePick(const std::optional<core::MarkupNote>& picked);
  std::optional<gp_Pnt> onSketchPlane(const NCollection_Vec2<int>& pixel) const;
  void showLiveStroke();
  double devicePixelRatio() const;

  OcctViewport* _viewport;
  SceneManager* _scene;
  core::Markup _markup;
  std::vector<Item> _items;
  std::vector<int> _missing;
  std::vector<gp_Vec> _offsets; // exploded view, by NodeId
  bool _shown = true;
  int _emphasized = 0;

  bool _picking = false;
  bool _updatingSelection = false;

  bool _sketching = false;
  bool _drawing = false;
  gp_Pln _sketchPlane;
  std::string _sketchPart;
  std::vector<gp_Pnt> _livePoints; // world
  NCollection_Vec2<int> _lastPixel;
  OccHandle<AIS_InteractiveObject> _liveStroke;
};

} // namespace sc::render
