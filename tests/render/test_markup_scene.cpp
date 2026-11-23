// MarkupScene (docs/designs/markup.md): notes and strokes draw over the model and in image
// exports, follow their part's visibility, and the note pick and sketching make new items.

#include "RenderTestUtils.h"
#include "TestModels.h"

#include "core/AssemblyIndex.h"
#include "core/Markup.h"
#include "render/MarkupScene.h"
#include "render/OcctViewport.h"
#include "render/SceneManager.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <Graphic3d_Camera.hxx>

#include <QtTest>

#include <algorithm>
#include <functional>
#include <memory>

using sc::core::AssemblyIndex;
using sc::core::NodeId;
using sc::render::MarkupScene;
using sc::render::SceneManager;

namespace {

NodeId findNode(const AssemblyIndex& index, const std::string& name) {
  for (const auto& node : index.nodes()) {
    if (node.name == name) {
      return node.id;
    }
  }
  return sc::core::NoNode;
}

int countPixels(const QImage& image, const std::function<bool(const QColor&)>& matches) {
  int count = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      count += matches(image.pixelColor(x, y)) ? 1 : 0;
    }
  }
  return count;
}

bool isNotePaper(const QColor& color) {
  return color.red() > 235 && color.green() > 225 && color.blue() > 120 && color.blue() < 190;
}

bool isStroke(const QColor& color) {
  return color.red() > 180 && color.green() < 90 && color.blue() > 110;
}

bool isEmphasis(const QColor& color) {
  return color.red() > 220 && color.green() > 90 && color.green() < 160 && color.blue() < 60;
}

} // namespace

class MarkupSceneTest : public QObject {
  Q_OBJECT

private:
  std::unique_ptr<sc::render::OcctViewport> _viewport;
  std::unique_ptr<SceneManager> _scene;
  std::unique_ptr<MarkupScene> _markup;
  std::shared_ptr<AssemblyIndex> _index;
  NodeId _plate = sc::core::NoNode;

  QImage render() {
    _viewport->invalidateScene();
    return _viewport->grabFramebuffer();
  }

  NCollection_Vec2<int> pixelOf(const gp_Pnt& point) const {
    int x = 0;
    int y = 0;
    _viewport->view()->Convert(point.X(), point.Y(), point.Z(), x, y);
    return NCollection_Vec2<int>(x, y);
  }

  // A note at the middle of the plate's top and a stroke across it, both on the plate.
  sc::core::Markup sample(int* noteId = nullptr, int* strokeId = nullptr) const {
    sc::core::Markup markup;
    const std::string plate = _index->pathId(_plate);
    const int note = markup.addNote({0, "Burr on this edge\nremove before paint", plate, gp_Pnt(50, 30, 10)});
    const int stroke = markup.addStroke(
        {0, plate, {gp_Pnt(10, 10, 10), gp_Pnt(40, 40, 10), gp_Pnt(70, 20, 10), gp_Pnt(90, 50, 10)}});
    if (noteId != nullptr) {
      *noteId = note;
    }
    if (strokeId != nullptr) {
      *strokeId = stroke;
    }
    return markup;
  }

private Q_SLOTS:
  void init() {
    _viewport = std::make_unique<sc::render::OcctViewport>();
    _viewport->resize(500, 400);
    _viewport->show();
    QVERIFY(QTest::qWaitForWindowExposed(_viewport.get()));
    _viewport->grabFramebuffer();
    QVERIFY(_viewport->isGlReady());
    _index = std::make_shared<AssemblyIndex>(sc::test::buildFixtureAssembly());
    for (const NodeId part : _index->partInstances()) {
      BRepMesh_IncrementalMesh(_index->prototypeShape(part), 0.05);
    }
    _scene = std::make_unique<SceneManager>(_viewport->context());
    _scene->setModel(_index);
    _markup = std::make_unique<MarkupScene>(_viewport.get(), _scene.get());
    _plate = findNode(*_index, "Base plate");
    QVERIFY(_plate != sc::core::NoNode);
    _viewport->setStandardView(V3d_XposYnegZpos);
    _viewport->fitAll();
  }

  void cleanup() {
    _markup.reset();
    _scene.reset();
    _viewport.reset();
    _index.reset();
  }

  void drawsNotesAndStrokesOverTheModel() {
    // A few pixels of the empty view (the view cube) have colors like the markup's.
    const QImage empty = render();
    const int paper0 = countPixels(empty, isNotePaper);
    const int ink0 = countPixels(empty, isStroke);
    const int emphasis0 = countPixels(empty, isEmphasis);
    int note = 0;
    int stroke = 0;
    _markup->setMarkup(sample(&note, &stroke));
    QCOMPARE(_markup->displayedCount(), 2);
    QVERIFY(_markup->isDisplayed(note));
    QVERIFY(_markup->isDisplayed(stroke));
    const QImage shown = render();
    sc::test::dumpImage(shown, "markup_view");
    const int paper = countPixels(shown, isNotePaper) - paper0;
    const int ink = countPixels(shown, isStroke) - ink0;
    QVERIFY2(paper > 1500, qPrintable(QStringLiteral("note pixels: %1").arg(paper)));
    QVERIFY2(ink > 100, qPrintable(QStringLiteral("stroke pixels: %1").arg(ink)));

    // Image export draws them too.
    const QImage exported = _viewport->renderImage(500, 400, sc::render::ExportBackground::White);
    QVERIFY(countPixels(exported, isNotePaper) > paper0 + 1500);
    QVERIFY(countPixels(exported, isStroke) > ink0 + 100);

    // The emphasized stroke is orange.
    const int emphasis = countPixels(shown, isEmphasis);
    _markup->setEmphasized(stroke);
    QVERIFY(countPixels(render(), isEmphasis) > std::max(emphasis, emphasis0) + 100);
    _markup->setEmphasized(0);

    // The box is placed by the stored point and the world box is there for zooming.
    const Bnd_Box box = _markup->worldBox(stroke);
    QVERIFY(!box.IsVoid());
    QVERIFY(!box.IsOut(gp_Pnt(40, 40, 10)));
  }

  void followsVisibility() {
    const QImage empty = render();
    const int paper0 = countPixels(empty, isNotePaper);
    const int ink0 = countPixels(empty, isStroke);
    int note = 0;
    int stroke = 0;
    _markup->setMarkup(sample(&note, &stroke));
    _scene->setVisible({_plate}, false);
    const QImage plateHidden = render(); // stale markup: still drawn until updated
    _markup->updateVisibility();
    QCOMPARE(_markup->displayedCount(), 0);
    QVERIFY(!_markup->isDisplayed(note));
    QCOMPARE(countPixels(render(), isNotePaper), paper0);
    QVERIFY(countPixels(plateHidden, isNotePaper) > paper0);

    _scene->setVisible({_plate}, true);
    _markup->updateVisibility();
    QCOMPARE(_markup->displayedCount(), 2);

    _markup->setShown(false);
    QCOMPARE(_markup->displayedCount(), 0);
    QCOMPARE(countPixels(render(), isStroke), ink0);
    _markup->setShown(true);
    QCOMPARE(_markup->displayedCount(), 2);

    // An item on a part the model does not have is kept but not drawn.
    sc::core::Markup markup = sample();
    const int lost = markup.addNote({0, "gone", "9:9:9", gp_Pnt()});
    const int world = markup.addStroke({0, "", {gp_Pnt(0, 0, 50), gp_Pnt(100, 0, 50)}});
    _markup->setMarkup(markup);
    QVERIFY(_markup->isMissing(lost));
    QVERIFY(!_markup->isDisplayed(lost));
    QVERIFY(_markup->isDisplayed(world)); // no part: always shown
    QCOMPARE(_markup->displayedCount(), 3);

    _markup->clear();
    QCOMPARE(_markup->displayedCount(), 0);
    QVERIFY(_markup->markup().empty());
  }

  void notePickGivesThePointOnThePart() {
    std::optional<sc::core::MarkupNote> picked;
    bool ended = false;
    connect(_markup.get(), &MarkupScene::notePicked, this,
            [&picked](const sc::core::MarkupNote& note) { picked = note; });
    connect(_markup.get(), &MarkupScene::notePickEnded, this, [&ended](bool) { ended = true; });
    _markup->startNotePick();
    QVERIFY(_markup->isPickingNote());
    render();

    const NCollection_Vec2<int> pixel = pixelOf(gp_Pnt(50, 30, 10));
    const auto& context = _viewport->context();
    context->MoveTo(pixel.x(), pixel.y(), _viewport->view(), false);
    QVERIFY(context->HasDetected());
    context->SelectDetected(AIS_SelectionScheme_Replace);
    _markup->onSelectionChanged();

    QVERIFY(ended);
    QVERIFY(!_markup->isPickingNote());
    QVERIFY(picked);
    QCOMPARE(picked->part, _index->pathId(_plate));
    QVERIFY2(
        picked->point.Distance(gp_Pnt(50, 30, 10)) < 1.0,
        qPrintable(
            QStringLiteral("%1 %2 %3").arg(picked->point.X()).arg(picked->point.Y()).arg(picked->point.Z())));
    QVERIFY(_scene->selectionModes().empty()); // whole parts again

    // Cancel ends a pick without a note.
    picked.reset();
    ended = false;
    _markup->startNotePick();
    _markup->cancelNotePick();
    QVERIFY(ended);
    QVERIFY(!picked);
  }

  void sketchingDrawsOnThePlaneUnderThePress() {
    std::optional<sc::core::MarkupStroke> drawn;
    connect(_markup.get(), &MarkupScene::strokeDrawn, this,
            [&drawn](const sc::core::MarkupStroke& stroke) { drawn = stroke; });
    _markup->setSketching(true);
    QCOMPARE(_viewport->pressHandler(), static_cast<sc::render::DragHandler*>(_markup.get()));
    const int ink0 = countPixels(render(), isStroke);

    const gp_Pnt start(50, 30, 10);
    const NCollection_Vec2<int> from = pixelOf(start);
    const auto& context = _viewport->context();
    context->MoveTo(from.x(), from.y(), _viewport->view(), false);
    QVERIFY(context->HasDetected());
    QVERIFY(_markup->beginDrag(context->DetectedInteractive(), from));
    for (int i = 1; i <= 20; ++i) {
      _markup->drag(from, NCollection_Vec2<int>(from.x() + 3 * i, from.y() - i));
    }
    _markup->drag(from, NCollection_Vec2<int>(from.x() + 60, from.y() - 20)); // too close: dropped
    QVERIFY(countPixels(render(), isStroke) > ink0 + 30);                     // drawn while dragging
    _markup->endDrag();

    QVERIFY(drawn);
    QCOMPARE(drawn->part, _index->pathId(_plate));
    QCOMPARE(drawn->points.size(), size_t(21));
    QVERIFY(drawn->points.front().Distance(start) < 1.0);
    // Every point lies on the plane facing the camera through the start.
    const gp_Dir direction = _viewport->view()->Camera()->Direction();
    for (const gp_Pnt& point : drawn->points) {
      QVERIFY(std::abs(gp_Vec(start, point).Dot(gp_Vec(direction))) < 1.0);
    }
    QCOMPARE(countPixels(render(), isStroke), ink0); // the live stroke is gone once finished

    // A press that is only a click makes no stroke.
    drawn.reset();
    QVERIFY(_markup->beginDrag(context->DetectedInteractive(), from));
    _markup->endDrag();
    QVERIFY(!drawn);

    // Over empty space: a world stroke.
    context->MoveTo(2, 2, _viewport->view(), false);
    QVERIFY(_markup->beginDrag(nullptr, NCollection_Vec2<int>(2, 2)));
    _markup->drag(NCollection_Vec2<int>(2, 2), NCollection_Vec2<int>(40, 40));
    _markup->endDrag();
    QVERIFY(drawn);
    QVERIFY(drawn->part.empty());

    _markup->setSketching(false);
    QCOMPARE(_viewport->pressHandler(), static_cast<sc::render::DragHandler*>(nullptr));
    QVERIFY(!_markup->beginDrag(nullptr, NCollection_Vec2<int>(2, 2)));
  }
};

int sc::test::runMarkupSceneTests(int argc, char** argv) {
  MarkupSceneTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_markup_scene.moc"
