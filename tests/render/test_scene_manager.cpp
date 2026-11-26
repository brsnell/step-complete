// SceneManager tests: instancing, visibility, selection mapping and rendering
// of an assembly built from the shared fixture.

#include "RenderTestUtils.h"
#include "TestModels.h"

#include "core/AssemblyIndex.h"
#include "core/ExplodeLayout.h"
#include "render/AisCompat.h"
#include "render/OcctViewport.h"
#include "render/SceneManager.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <TopExp_Explorer.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <QtTest>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>

using sc::OccHandle;
using sc::core::AssemblyIndex;
using sc::core::NodeId;
using sc::render::SceneManager;

namespace {

NodeId findNode(const AssemblyIndex& index, const std::string& name, int occurrence = 0) {
  for (const auto& node : index.nodes()) {
    if (node.name == name && occurrence-- == 0) {
      return node.id;
    }
  }
  return sc::core::NoNode;
}

const std::string Pin = "Pin \xC3\x98"
                        "6";

// Visible part objects currently displayed by the context.
int displayedParts(const SceneManager& scene, const OccHandle<AIS_InteractiveContext>& context) {
  sc::occ::InteractiveList objects;
  context->DisplayedObjects(objects);
  int count = 0;
  for (sc::occ::InteractiveList::Iterator it(objects); it.More(); it.Next()) {
    if (scene.partOf(it.Value()) != sc::core::NoNode) {
      ++count;
    }
  }
  return count;
}

// Pixels whose color leans clearly to one channel (blue: the override in these tests).
int countPixels(const QImage& image, const std::function<bool(const QColor&)>& matches) {
  int count = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      count += matches(image.pixelColor(x, y)) ? 1 : 0;
    }
  }
  return count;
}

bool isBlue(const QColor& color) {
  return color.blue() > 120 && color.blue() > color.red() + 60 && color.blue() > color.green() + 60;
}

bool isRed(const QColor& color) {
  return color.red() > 120 && color.red() > color.green() + 60 && color.red() > color.blue() + 60;
}

bool isMagenta(const QColor& color) {
  return color.red() > 140 && color.blue() > 140 && color.green() + 80 < std::min(color.red(), color.blue());
}

} // namespace

class SceneManagerTest : public QObject {
  Q_OBJECT

private:
  std::unique_ptr<sc::render::OcctViewport> _viewport;
  std::unique_ptr<SceneManager> _scene;
  std::shared_ptr<AssemblyIndex> _index;

  QImage render() {
    _viewport->invalidateScene();
    return _viewport->grabFramebuffer();
  }

private Q_SLOTS:
  void init() {
    _viewport = std::make_unique<sc::render::OcctViewport>();
    _viewport->resize(400, 300);
    _viewport->show();
    QVERIFY(QTest::qWaitForWindowExposed(_viewport.get()));
    _viewport->grabFramebuffer();
    QVERIFY(_viewport->isGlReady());

    _index = std::make_shared<AssemblyIndex>(sc::test::buildFixtureAssembly());
    _scene = std::make_unique<SceneManager>(_viewport->context());
    _scene->setModel(_index);
    _viewport->fitAll();
  }

  void cleanup() {
    _scene.reset(); // removes presentations while the context is alive
    _viewport.reset();
    _index.reset();
  }

  void sharesPrototypesBetweenInstances() {
    // Plate, plain pin, green pin: 5 instances share 3 presentations.
    QCOMPARE(_scene->prototypeCount(), 3);
    QCOMPARE(_scene->detachedCount(), 0);
  }

  void respectsVisibilityFromFile() {
    // The second pin pair is hidden in the document.
    QCOMPARE(displayedParts(*_scene, _viewport->context()), 3);
    QVERIFY(!_scene->isVisible(findNode(*_index, "Pin pair", 1)));
    QVERIFY(_scene->isVisible(_index->roots().front()));
  }

  void hidesIsolatesAndShowsAll() {
    const NodeId plate = findNode(*_index, "Base plate");
    _scene->isolate({plate});
    QCOMPARE(displayedParts(*_scene, _viewport->context()), 1);
    QVERIFY(_scene->isVisible(plate));

    _scene->setVisible({plate}, false);
    QCOMPARE(displayedParts(*_scene, _viewport->context()), 0);

    _scene->showAll();
    QCOMPARE(displayedParts(*_scene, _viewport->context()), 5);
  }

  void mapsSelectionBothWays() {
    const NodeId pair = findNode(*_index, "Pin pair", 0);
    _scene->select({pair});
    const auto selected = _scene->selectedParts();
    QCOMPARE(int(selected.size()), 2);
    for (const NodeId id : selected) {
      QCOMPARE(_index->node(id).name, Pin);
      QCOMPARE(_index->node(id).parent, pair);
    }
    _scene->select({});
    QVERIFY(_scene->selectedParts().empty());
  }

  void computesWorldBoundingBoxes() {
    const NodeId pin = findNode(*_index, Pin, 1); // (10,10,10) + (20,0,0), r = 3
    const Bnd_Box box = _scene->boundingBox({pin});
    QVERIFY(!box.IsVoid());
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    QVERIFY2(qAbs((xmin + xmax) / 2 - 30.0) < 0.5, qPrintable(QString::number((xmin + xmax) / 2)));
    QVERIFY(qAbs(zmin - 10.0) < 0.5);
  }

  void offsetsMoveThePartsAndPutThemBack() {
    // Exploded view (docs/designs/exploded-view.md): a translation per node on top of the placement.
    const NodeId pin = findNode(*_index, Pin, 1);
    const auto centerX = [this](NodeId node) {
      double xmin, ymin, zmin, xmax, ymax, zmax;
      _scene->boundingBox({node}).Get(xmin, ymin, zmin, xmax, ymax, zmax);
      return (xmin + xmax) / 2;
    };
    QVERIFY(qAbs(centerX(pin) - 30.0) < 0.5);
    QVERIFY(!_scene->isExploded());

    std::vector<gp_Vec> offsets(_index->nodes().size(), gp_Vec(0.0, 0.0, 0.0));
    offsets[static_cast<size_t>(pin)] = gp_Vec(100.0, 0.0, 0.0);
    _scene->setPartOffsets(offsets);
    QVERIFY(_scene->isExploded());
    QVERIFY2(qAbs(centerX(pin) - 130.0) < 0.5, qPrintable(QString::number(centerX(pin))));
    QVERIFY(qAbs(_scene->displayedPlacement(pin).TranslationPart().X() - 130.0) < 1e-9);
    const NodeId plate = findNode(*_index, "Base plate");
    QVERIFY(qAbs(centerX(plate) - 50.0) < 0.5); // no offset of its own

    // A part detached while exploded (transparency) keeps its offset.
    _scene->setTransparency({pin}, 0.5);
    QCOMPARE(_scene->detachedCount(), 1);
    QVERIFY(qAbs(centerX(pin) - 130.0) < 0.5);

    // Picking finds the part where it is drawn.
    _viewport->fitAll();
    render();
    int px = 0;
    int py = 0;
    _viewport->view()->Convert(130.0, 10.0, 30.0, px, py); // the pin's top, moved
    _viewport->context()->MoveTo(px, py, _viewport->view(), false);
    QVERIFY(_viewport->context()->HasDetected());
    QCOMPARE(_scene->partOf(_viewport->context()->DetectedInteractive()), pin);

    // The whole fixture by its layout, for a look (SC_RENDER_DUMP_DIR).
    _scene->setTransparency({pin}, 0.0);
    _scene->setPartOffsets(sc::core::ExplodeLayout(*_index).offsets(0.6));
    _viewport->setStandardView(V3d_XposYnegZpos);
    _viewport->fitAll();
    sc::test::dumpImage(render(), "exploded_fixture");

    _scene->setPartOffsets({});
    QVERIFY(!_scene->isExploded());
    QVERIFY(qAbs(centerX(pin) - 30.0) < 0.5);
  }

  void partColorReplacesTheFilesColorsAndResets() {
    // docs/designs/part-color-overrides.md: one color for the whole instance, its twins keep theirs.
    _scene->showAll();
    const NodeId plate = findNode(*_index, "Base plate");
    const NodeId pin = findNode(*_index, Pin, 0);
    const NodeId twin = findNode(*_index, Pin, 2); // same definition, the other pair
    QVERIFY(plate != sc::core::NoNode && pin != sc::core::NoNode && twin != sc::core::NoNode);
    QCOMPARE(_index->node(pin).prototypeLabel, _index->node(twin).prototypeLabel);
    const QImage before = render();
    const int red = countPixels(before, isRed); // the plate's file color
    QVERIFY(red > 500);
    QCOMPARE(countPixels(before, isBlue), 0);

    const Quantity_Color blue(0.1, 0.2, 0.9, Quantity_TOC_sRGB);
    _scene->setPartColor({plate, pin}, blue);
    QVERIFY(_scene->partColor(plate).has_value());
    QVERIFY(_scene->partColor(pin).has_value());
    QVERIFY(!_scene->partColor(twin).has_value());
    QCOMPARE(_scene->detachedCount(), 2); // the twin still shares its prototype
    const QImage colored = render();
    sc::test::dumpImage(colored, "scene_part_color");
    QVERIFY2(countPixels(colored, isBlue) > red / 2,
             qPrintable(QString::number(countPixels(colored, isBlue))));
    QVERIFY(countPixels(colored, isRed) < red / 20);

    // A display style change rebuilds the styles: the color stays.
    _scene->setDisplayStyle(sc::render::DisplayStyle::Wireframe);
    render();
    _scene->setDisplayStyle(sc::render::DisplayStyle::ShadedWithEdges);
    QVERIFY(countPixels(render(), isBlue) > red / 2);
    // So does transparency, and its own color comes back with it off.
    _scene->setTransparency({plate}, 0.5);
    _scene->setTransparency({plate}, 0.0);
    QVERIFY(countPixels(render(), isBlue) > red / 2);

    _scene->setPartColor({plate, pin}, std::nullopt);
    QVERIFY(!_scene->partColor(plate).has_value());
    const QImage reset = render();
    QVERIFY(countPixels(reset, isRed) > red * 9 / 10);
    QCOMPARE(countPixels(reset, isBlue), 0);
  }

  void partColorCoversPerFaceFileColors() {
    // Faces the file colors themselves (magenta: nothing else in the view is) take the override too.
    auto document = sc::test::buildFixtureAssembly();
    const AssemblyIndex lookup(document);
    const TDF_Label plateLabel = lookup.node(findNode(lookup, "Base plate")).prototypeLabel;
    auto shapes = XCAFDoc_DocumentTool::ShapeTool(document->ocaf()->Main());
    auto colors = XCAFDoc_DocumentTool::ColorTool(document->ocaf()->Main());
    for (TopExp_Explorer it(XCAFDoc_ShapeTool::GetShape(plateLabel), TopAbs_FACE); it.More(); it.Next()) {
      colors->SetColor(shapes->AddSubShape(plateLabel, it.Current()), Quantity_Color(Quantity_NOC_MAGENTA1),
                       XCAFDoc_ColorSurf);
    }
    _index = std::make_shared<AssemblyIndex>(document);
    _scene->setModel(_index);
    _viewport->fitAll();
    const NodeId plate = findNode(*_index, "Base plate");
    QVERIFY(countPixels(render(), isMagenta) > 500);
    _scene->setPartColor({plate}, Quantity_Color(0.1, 0.2, 0.9, Quantity_TOC_sRGB));
    const QImage colored = render();
    sc::test::dumpImage(colored, "scene_part_color_faces");
    QCOMPARE(countPixels(colored, isMagenta), 0);
    QVERIFY(countPixels(colored, isBlue) > 500);
    _scene->setPartColor({plate}, std::nullopt);
    QVERIFY(countPixels(render(), isMagenta) > 500);
  }

  void detachesInstancesForTransparency() {
    const NodeId pin = findNode(*_index, Pin, 0);
    _scene->select({pin});
    _scene->setTransparency({pin}, 0.6);
    QCOMPARE(_scene->detachedCount(), 1);
    QVERIFY(_scene->isVisible(pin));
    QCOMPARE(displayedParts(*_scene, _viewport->context()), 3);
    QCOMPARE(_scene->selectedParts(), std::vector<NodeId>{pin}); // selection survives detaching
  }

  void highlightsSelectedInstances() {
    const NodeId plate = findNode(*_index, "Base plate");
    const QImage before = render();
    _scene->select({plate});
    const QImage after = render();
    sc::test::dumpImage(after, "scene_selected_plate");
    const double changed = sc::test::changedPixelFraction(before, after);
    qInfo("selection changed %.1f%% of pixels", changed * 100.0);
    QVERIFY2(changed > 0.15, "selected part is not visibly highlighted");

    // ... and in the selection color (blue), not OCCT's default gray.
    const QColor center = after.pixelColor(after.width() / 2, after.height() * 2 / 3);
    QVERIFY2(center.blue() > center.red() + 60, qPrintable(center.name()));
  }

  void rendersAssemblyAndSwitchesStyles() {
    const QImage shadedEdges = render();
    sc::test::dumpImage(shadedEdges, "scene_shaded_edges");

    // Something rendered, including the green instance color.
    int green = 0;
    for (int y = 0; y < shadedEdges.height(); ++y) {
      for (int x = 0; x < shadedEdges.width(); ++x) {
        const QColor color = shadedEdges.pixelColor(x, y);
        if (color.green() > 120 && color.green() > color.red() + 60 && color.green() > color.blue() + 60) {
          ++green;
        }
      }
    }
    QVERIFY2(green > 20, "instance color (green pin) is not rendered");

    _scene->setDisplayStyle(sc::render::DisplayStyle::Wireframe);
    const QImage wireframe = render();
    sc::test::dumpImage(wireframe, "scene_wireframe");
    QVERIFY2(sc::test::changedPixelFraction(shadedEdges, wireframe) > 0.05, "wireframe looks like shaded");

    _scene->setDisplayStyle(sc::render::DisplayStyle::Shaded);
    const QImage shaded = render();
    QVERIFY2(sc::test::changedPixelFraction(shadedEdges, shaded) > 0.002,
             "edges are not toggled on instances");
  }

  // Measure > Check Geometry: a problem is drawn in red on top, not selectable, and goes away
  // on clearIssue and with the model.
  void drawsAndClearsTheGeometryIssueHighlight() {
    const QImage before = render();
    // One face of the plate, where it is: a red patch over the plate.
    const NodeId plate = findNode(*_index, "Base plate");
    const TopoDS_Shape face = TopExp_Explorer(_index->worldShape(plate), TopAbs_FACE).Current();
    _scene->showIssue(face);
    QVERIFY(_scene->hasIssue());
    const QImage shown = render();
    sc::test::dumpImage(shown, "scene_issue_highlight");
    int red = 0;
    for (int y = 0; y < shown.height(); ++y) {
      for (int x = 0; x < shown.width(); ++x) {
        const QColor color = shown.pixelColor(x, y);
        red += color.red() > 180 && color.green() < 80 && color.blue() < 80 ? 1 : 0;
      }
    }
    QVERIFY2(red > 100, "the issue highlight is not drawn in red");
    QCOMPARE(displayedParts(*_scene, _viewport->context()), 3); // not a part

    _scene->clearIssue();
    QVERIFY(!_scene->hasIssue());
    QVERIFY2(sc::test::changedPixelFraction(before, render()) < 0.001, "the highlight is still drawn");

    // A new model, or a null shape, clears it too.
    _scene->showIssue(BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape());
    _scene->setModel(_index);
    QVERIFY(!_scene->hasIssue());
    _scene->showIssue(face);
    _scene->showIssue(TopoDS_Shape());
    QVERIFY(!_scene->hasIssue());
  }

  void hiddenLineDrawsWhiteFacesAndRestoresColors() {
    std::vector<NodeId> shown;
    for (const NodeId part : _index->partInstances()) {
      if (_scene->isVisible(part)) {
        shown.push_back(part);
      }
    }
    _scene->setVisible(_index->partInstances(), false);
    const QImage background = render();
    _scene->setVisible(shown, true);
    // A transparent part is detached from its prototype; it must follow the style too.
    _scene->setTransparency({shown.back()}, 0.6);
    const QImage before = render();

    _scene->setDisplayStyle(sc::render::DisplayStyle::HiddenLine);
    const QImage hiddenLine = render();
    sc::test::dumpImage(hiddenLine, "scene_hidden_line");

    // Over the parts (pixels that differ from the empty background) there is no
    // part color left: only white faces and dark lines.
    int partPixels = 0;
    int white = 0;
    int colored = 0;
    for (int y = 0; y < hiddenLine.height(); ++y) {
      for (int x = 0; x < hiddenLine.width(); ++x) {
        const QColor color = hiddenLine.pixelColor(x, y);
        const QColor empty = background.pixelColor(x, y);
        if (qAbs(color.red() - empty.red()) + qAbs(color.green() - empty.green()) +
                qAbs(color.blue() - empty.blue()) <=
            24) {
          continue;
        }
        ++partPixels;
        if (color.red() > 245 && color.green() > 245 && color.blue() > 245) {
          ++white;
        }
        if (color.saturation() > 80 && color.value() > 80) {
          ++colored;
        }
      }
    }
    qInfo("hidden line: %d part pixels, %d white, %d colored", partPixels, white, colored);
    QVERIFY2(partPixels > 2000, "parts are not drawn in hidden-line mode");
    QVERIFY2(white > partPixels / 2, "faces are not white in hidden-line mode");
    QVERIFY2(colored < partPixels / 100, "part colors still show in hidden-line mode");

    // Leaving hidden line brings back the file's colors and the lit shading.
    _scene->setDisplayStyle(sc::render::DisplayStyle::ShadedWithEdges);
    const QImage after = render();
    sc::test::dumpImage(after, "scene_after_hidden_line");
    const double changed = sc::test::changedPixelFraction(before, after);
    qInfo("shaded with edges before vs after hidden line: %.2f%% changed", changed * 100.0);
    QVERIFY2(changed < 0.001, "colors or shading are not restored after hidden line");
  }
};

int sc::test::runSceneManagerTests(int argc, char** argv) {
  SceneManagerTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_scene_manager.moc"
