// PmiScene tests: PMI drawn on every instance of its part, visibility, picking,
// cross-highlighting of target geometry, and saved views.

#include "PmiFixture.h"
#include "RenderTestUtils.h"

#include "core/AssemblyIndex.h"
#include "core/PmiModel.h"
#include "core/StepImporter.h"
#include "render/OcctViewport.h"
#include "render/PmiFont.h"
#include "render/PmiLabel.h"
#include "render/PmiScene.h"
#include "render/SceneManager.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRep_Builder.hxx>
#include <Font_FTFont.hxx>
#include <Font_FontAspect.hxx>
#include <Font_FontMgr.hxx>
#include <Font_SystemFont.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TDataStd_Name.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFDimTolObjects_DimensionObject.hxx>
#include <XCAFDoc_DimTolTool.hxx>
#include <XCAFDoc_Dimension.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Trsf.hxx>

#include <QFile>
#include <QRawFont>
#include <QtTest>

#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <algorithm>
#include <climits>
#include <memory>

using sc::core::PmiId;
using sc::core::PmiModel;

namespace {

int countPixels(const QImage& image, bool (*match)(const QColor&)) {
  int count = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      count += match(image.pixelColor(x, y)) ? 1 : 0;
    }
  }
  return count;
}

bool isPmiBlue(const QColor& c) {
  return c.blue() > 90 && c.blue() > c.red() + 50 && c.blue() > c.green() + 30 && c.red() < 80;
}

// Ink of a label: dark, bluish pixels (not the white box or the gray part).
bool isLabelInk(const QColor& c) {
  return c.blue() > c.red() + 60 && c.blue() < 200 && c.red() < 120;
}

bool isTargetOrange(const QColor& c) {
  return c.red() > 200 && c.green() > 100 && c.green() < 200 && c.blue() < 90;
}

} // namespace

class PmiSceneTest : public QObject {
  Q_OBJECT

private:
  std::unique_ptr<sc::render::OcctViewport> m_viewport;
  std::unique_ptr<sc::render::SceneManager> m_scene;
  std::unique_ptr<sc::render::PmiScene> m_pmiScene;
  std::shared_ptr<PmiModel> m_pmi;
  sc::test::PmiFixture m_fixture;

  QImage render() {
    m_viewport->invalidateScene();
    return m_viewport->grabFramebuffer();
  }

  // Puts the PMI part into an assembly twice: at the origin and 150 mm along X.
  void makeTwoInstances() {
    const TDF_Label main = m_fixture.document->ocaf()->Main();
    auto shapes = XCAFDoc_DocumentTool::ShapeTool(main);
    const TDF_Label assembly = shapes->NewShape();
    TDataStd_Name::Set(assembly, "Pair");
    shapes->AddComponent(assembly, m_fixture.part, TopLoc_Location());
    gp_Trsf shift;
    shift.SetTranslation(gp_Vec(150, 0, 0));
    shapes->AddComponent(assembly, m_fixture.part, TopLoc_Location(shift));
    shapes->UpdateAssemblies();
  }

  void load() {
    auto index = std::make_shared<sc::core::AssemblyIndex>(m_fixture.document);
    m_scene->setModel(index);
    m_pmi = std::make_shared<PmiModel>(m_fixture.document);
    m_pmiScene->setModel(m_pmi, index);
    m_viewport->setStandardView(V3d_Zpos);
    m_viewport->fitAll();
  }

  PmiId id(const TDF_Label& label) const { return m_pmi->itemFor(label); }

  // View pixel of a model point.
  QPoint pixelOf(double x, double y, double z) const {
    int xp = 0;
    int yp = 0;
    m_viewport->view()->Convert(x, y, z, xp, yp);
    return {xp, yp};
  }

  // PMI picked at a view pixel, or kNoPmi.
  PmiId pmiAt(const QPoint& p) {
    const auto& context = m_viewport->context();
    context->MoveTo(p.x(), p.y(), m_viewport->view(), false);
    return context->HasDetected() ? m_pmiScene->pmiOf(context->DetectedInteractive()) : sc::core::kNoPmi;
  }

private Q_SLOTS:
  void init() {
    m_viewport = std::make_unique<sc::render::OcctViewport>();
    m_viewport->resize(500, 400);
    m_viewport->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_viewport.get()));
    m_viewport->grabFramebuffer();
    QVERIFY(m_viewport->isGlReady());
    m_fixture = sc::test::buildPmiFixture();
    m_scene = std::make_unique<sc::render::SceneManager>(m_viewport->context());
    m_pmiScene = std::make_unique<sc::render::PmiScene>(m_viewport->context());
  }

  void cleanup() {
    m_pmiScene.reset();
    m_scene.reset();
    m_viewport.reset();
  }

  void displaysEveryItemOnEveryInstance() {
    makeTwoInstances();
    load();
    QCOMPARE(m_pmiScene->presentationCount(), static_cast<int>(m_pmi->items().size()) * 2);
    QCOMPARE(m_pmiScene->visibleIds().size(), m_pmi->items().size());
  }

  void drawsGraphicalAndSemanticPmiAndHidesIt() {
    load();
    const QImage shown = render();
    sc::test::dumpImage(shown, "pmi_shown");
    m_pmiScene->setVisible(m_pmiScene->visibleIds(), false);
    QVERIFY(m_pmiScene->visibleIds().empty());
    const QImage hidden = render();
    QVERIFY2(countPixels(shown, isPmiBlue) > countPixels(hidden, isPmiBlue) + 50,
             "PMI (presentation lines and labels) is not drawn");
    QVERIFY(sc::test::changedPixelFraction(shown, hidden) > 0.002);

    m_pmiScene->setVisible({id(m_fixture.flatness)}, true);
    QCOMPARE(m_pmiScene->visibleIds(), std::vector<PmiId>{id(m_fixture.flatness)});
    QVERIFY(m_pmiScene->isVisible(id(m_fixture.flatness)));
    QVERIFY(!m_pmiScene->isVisible(id(m_fixture.height)));
  }

  void highlightsTheGeometryAnAnnotationAppliesTo() {
    load();
    const QImage before = render();
    QCOMPARE(countPixels(before, isTargetOrange), 0);
    m_pmiScene->highlight({id(m_fixture.flatness)}); // the top face
    const QImage after = render();
    sc::test::dumpImage(after, "pmi_target_highlight");
    QVERIFY2(countPixels(after, isTargetOrange) > after.width() * after.height() / 10,
             "the top face is not highlighted");
    QCOMPARE(m_pmiScene->selectedPmi(), std::vector<PmiId>{id(m_fixture.flatness)});

    m_pmiScene->highlight({});
    QCOMPARE(countPixels(render(), isTargetOrange), 0);
  }

  void bundledFontCoversTheGdtSymbols() {
    QVERIFY(sc::render::ensurePmiFont());
    QCOMPARE(QString::fromLatin1(sc::render::pmiFontName()), QStringLiteral("STIX Two Math"));
    // OCCT resolves the family to the registered font file...
    const sc::OccHandle<Font_SystemFont> system =
        Font_FontMgr::GetInstance()->GetFont(TCollection_AsciiString(sc::render::pmiFontName()));
    QVERIFY(!system.IsNull());
    QVERIFY(!system->FontPath(Font_FontAspect_Regular).IsEmpty());
    const sc::OccHandle<Font_FTFont> font = Font_FTFont::FindAndCreate(
        sc::render::pmiFontName(), Font_FontAspect_Regular, Font_FTFontParams(13, 72));
    QVERIFY(!font.IsNull());

    // ...and that file has a glyph for every GD&T symbol (OCCT itself draws .notdef for a missing
    // one, so ask the font's character map).
    QFile resource(QStringLiteral(":/fonts/STIXTwoMath-Regular.ttf"));
    QVERIFY(resource.open(QIODevice::ReadOnly));
    const QRawFont raw(resource.readAll(), 13.0);
    QVERIFY(raw.isValid());
    QVERIFY(!raw.supportsCharacter(0x10FFFFu));
    // flatness, position, circularity, cylindricity, symmetry, total runout, profile of a surface,
    // perpendicularity, parallelism, angularity, diameter, concentricity, the material conditions
    const uint symbols[] = {0x23E5, 0x2316, 0x25CB, 0x232D, 0x232F, 0x2330, 0x2313, 0x27C2, 0x2225, 0x2220,
                            0x2300, 0x25CE, 0x24C2, 0x24C1, 0x24C5, 0x24BB, 0x2312, 0x2197, 0x00B1};
    for (const uint symbol : symbols) {
      QVERIFY2(raw.supportsCharacter(symbol),
               qPrintable(QStringLiteral("no glyph for U+%1").arg(symbol, 4, 16)));
    }
  }

  void stacksLabelsThatShareAnAnchor() {
    load();
    m_pmiScene->setOnlyVisible({id(m_fixture.flatness), id(m_fixture.parallelism)});
    const QImage frame = render();
    sc::test::dumpImage(frame, "pmi_stacked_labels");

    // Both items have no text position: the labels sit on the top face's center.
    const QPoint anchor = pixelOf(50, 30, 20);
    const int pitch = int(sc::render::PmiLabel::stackPitch(1.0));
    const PmiId first = pmiAt(anchor + QPoint(8, 0));
    const PmiId second = pmiAt(anchor + QPoint(8, pitch));
    QCOMPARE(first, std::min(id(m_fixture.flatness), id(m_fixture.parallelism)));
    QCOMPARE(second, std::max(id(m_fixture.flatness), id(m_fixture.parallelism)));
    QVERIFY(first != second);
    // Nothing is drawn above the first row or below the second.
    QCOMPARE(pmiAt(anchor + QPoint(8, -pitch)), sc::core::kNoPmi);
    QCOMPARE(pmiAt(anchor + QPoint(8, 2 * pitch)), sc::core::kNoPmi);
    // Left of the anchor is empty too: the box starts at the anchor.
    QCOMPARE(pmiAt(anchor + QPoint(-8, 0)), sc::core::kNoPmi);

    // The boxes are drawn apart: a gap separates the two rows of white interior.
    const int x = anchor.x() + int(sc::render::PmiLabel::rowHeight(1.0)) - 4;
    QVector<int> whiteRows;
    for (int dy = -20; dy <= 60; ++dy) {
      const QColor c = frame.pixelColor(x, anchor.y() + dy);
      if (c.red() > 240 && c.green() > 240 && c.blue() > 240) {
        whiteRows.push_back(dy);
      }
    }
    QVERIFY(whiteRows.size() > 20);
    bool gap = false;
    for (int i = 1; i < whiteRows.size(); ++i) {
      gap = gap || whiteRows[i] - whiteRows[i - 1] > 1;
    }
    QVERIFY2(gap, "the two labels are not separated by a gap");
  }

  void pickingALabelSelectsItsPmi() {
    load();
    // Show only the flatness: its label sits on the top face's center (no text position in the file).
    m_pmiScene->setOnlyVisible({id(m_fixture.flatness)});
    render();
    const QPoint anchor = pixelOf(50, 30, 20);
    QCOMPARE(pmiAt(anchor + QPoint(8, 0)), id(m_fixture.flatness));
    const auto& context = m_viewport->context();
    QVERIFY(m_scene->partOf(context->DetectedInteractive()) == sc::core::kNoNode);
    sc::test::dumpImage(render(), "pmi_label_hover");
    context->SelectDetected();
    QCOMPARE(m_pmiScene->selectedPmi(), std::vector<PmiId>{id(m_fixture.flatness)});
    context->MoveTo(5, 5, m_viewport->view(), false);
    const QImage selected = render();
    sc::test::dumpImage(selected, "pmi_label_selected");
    // The selected label gets an orange outline and stays legible (its white box is not filled).
    QVERIFY(countPixels(selected, isTargetOrange) > 60);
    QVERIFY(selected.pixelColor(anchor + QPoint(30, -9)).red() > 240);
  }

  void hidingAnItemHidesItsLabel() {
    load();
    m_pmiScene->setOnlyVisible({id(m_fixture.flatness), id(m_fixture.parallelism)});
    const QImage both = render();
    const QPoint anchor = pixelOf(50, 30, 20);
    const int pitch = int(sc::render::PmiLabel::stackPitch(1.0));
    const PmiId first = pmiAt(anchor + QPoint(8, 0));
    const PmiId second = pmiAt(anchor + QPoint(8, pitch));

    m_pmiScene->setVisible({first}, false);
    const QImage one = render();
    QCOMPARE(pmiAt(anchor + QPoint(8, 0)), sc::core::kNoPmi);
    QCOMPARE(pmiAt(anchor + QPoint(8, pitch)), second); // the other keeps its row
    QVERIFY(sc::test::changedPixelFraction(both, one) > 0.0005);

    m_pmiScene->setVisible({first}, true);
    QCOMPARE(pmiAt(anchor + QPoint(8, 0)), first);
    m_pmiScene->setVisible({first, second}, false);
    const QImage none = render();
    QCOMPARE(pmiAt(anchor + QPoint(8, 0)), sc::core::kNoPmi);
    QCOMPARE(pmiAt(anchor + QPoint(8, pitch)), sc::core::kNoPmi);
    QCOMPARE(countPixels(none, isLabelInk), 0);
  }

  void labelsHaveAConstantPixelSize() {
    load();
    m_pmiScene->setOnlyVisible({id(m_fixture.flatness)});
    const auto inkBox = [this]() {
      const QImage frame = render();
      const QPoint anchor = pixelOf(50, 30, 20);
      QRect box;
      for (int y = anchor.y() - 40; y < anchor.y() + 40; ++y) {
        for (int x = anchor.x() - 10; x < anchor.x() + 200; ++x) {
          if (isLabelInk(frame.pixelColor(x, y))) {
            box |= QRect(x, y, 1, 1);
          }
        }
      }
      return box;
    };
    const QRect before = inkBox();
    m_viewport->view()->SetScale(m_viewport->view()->Scale() * 0.5); // zoom in 2x
    const QRect zoomed = inkBox();
    QVERIFY(!before.isEmpty());
    QCOMPARE(zoomed.size(), before.size());
    // 1 local unit is 1 pixel: the row is rowHeight() pixels tall (plus the border).
    QVERIFY(std::abs(before.height() - int(sc::render::PmiLabel::rowHeight(1.0))) <= 2);
  }

  void scalesWithTheDevicePixelRatio() {
    auto index = std::make_shared<sc::core::AssemblyIndex>(m_fixture.document);
    m_scene->setModel(index);
    m_pmi = std::make_shared<PmiModel>(m_fixture.document);
    m_pmiScene->setModel(m_pmi, index, 2.0);
    m_viewport->setStandardView(V3d_Zpos);
    m_viewport->fitAll();
    m_pmiScene->setOnlyVisible({id(m_fixture.flatness), id(m_fixture.parallelism)});
    render();
    const QPoint anchor = pixelOf(50, 30, 20);
    const int pitch = int(sc::render::PmiLabel::stackPitch(2.0));
    QCOMPARE(sc::render::PmiLabel::rowHeight(2.0), 2.0 * sc::render::PmiLabel::rowHeight(1.0));
    QVERIFY(pmiAt(anchor + QPoint(8, 0)) != sc::core::kNoPmi);
    QVERIFY(pmiAt(anchor + QPoint(8, pitch)) != sc::core::kNoPmi);
    QCOMPARE(pmiAt(anchor + QPoint(8, 2 * pitch)), sc::core::kNoPmi);
    sc::test::dumpImage(render(), "pmi_labels_dpr2");
  }

  void drawsFramesDatumFlagsAndValues() {
    load();
    m_pmiScene->setOnlyVisible({});
    // A frame with symbols the default fonts lack, a datum flag and a dimension, on separate anchors.
    sc::core::PmiItem frame;
    frame.id = 100;
    frame.kind = sc::core::PmiKind::GeometricTolerance;
    frame.frame = {"⌖",
                   "⌀"
                   "0.1 Ⓜ",
                   "A", "B", "Ⓜ"};
    sc::core::PmiItem datum;
    datum.id = 101;
    datum.kind = sc::core::PmiKind::Datum;
    datum.text = "A";
    sc::core::PmiItem dimension;
    dimension.id = 102;
    dimension.kind = sc::core::PmiKind::Dimension;
    dimension.text = "⌀"
                     "12.70 ±0.05";
    sc::core::PmiItem frame2 = frame;
    frame2.id = 103;
    frame2.frame = {"⌓", "0.2", "A"};
    const auto show = [this](const sc::core::PmiItem& item, const gp_Pnt& at, int stack) {
      sc::OccHandle<sc::render::PmiLabel> label = new sc::render::PmiLabel(item, at, stack, 1.0);
      m_viewport->context()->Display(label, 0, 0, false);
      return label;
    };
    const auto framed = show(frame, gp_Pnt(10, 50, 20), 0);
    const auto stacked = show(frame2, gp_Pnt(10, 50, 20), 1);
    const auto flag = show(datum, gp_Pnt(10, 10, 20), 0);
    const auto value = show(dimension, gp_Pnt(60, 10, 20), 0);
    const QImage image = render();
    sc::test::dumpImage(image, "pmi_label_kinds");

    // Every cell of the frame has text ink inside it (nothing dropped, no empty boxes).
    const QPoint anchor = pixelOf(10, 50, 20);
    const auto extents = framed->cellExtents();
    QCOMPARE(extents.size(), size_t(5));
    for (const auto& cell : extents) {
      const QRect inside(anchor.x() + int(cell.first) + 3, anchor.y() - 8, int(cell.second - cell.first) - 6,
                         16);
      QRect inkBounds;
      for (int y = inside.top(); y <= inside.bottom(); ++y) {
        for (int x = inside.left(); x <= inside.right(); ++x) {
          if (image.pixelColor(x, y).red() < 190) { // darker than the white box
            inkBounds |= QRect(x, y, 1, 1);
          }
        }
      }
      QVERIFY2(!inkBounds.isNull() && inkBounds.width() >= 4 && inkBounds.height() >= 4,
               "a cell has no text");
    }
    QVERIFY(stacked->centerY() < framed->centerY());
    QVERIFY(flag->boxLeft() > 0.0);                      // datum stem
    QCOMPARE(flag->boxWidth(), flag->boxHeight());       // a square letter box
    QVERIFY(value->boxWidth() > value->boxHeight() * 2); // the value is wider than a square
  }

private:
  // Adds a semantic-only dimension (no file presentation) to the fixture document.
  TDF_Label addSemanticDimension(XCAFDimTolObjects_DimensionType type, const gp_Pnt& textAt,
                                 const std::optional<std::pair<gp_Pnt, gp_Pnt>>& points, double value) {
    auto gdt = XCAFDoc_DocumentTool::DimTolTool(m_fixture.document->ocaf()->Main());
    const TDF_Label dimension = gdt->AddDimension();
    sc::OccHandle<XCAFDimTolObjects_DimensionObject> object = new XCAFDimTolObjects_DimensionObject();
    object->SetType(type);
    object->SetValue(value);
    object->SetPlane(gp_Ax2(gp_Pnt(0, 0, 20), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)));
    object->SetPointTextAttach(textAt);
    if (points) {
      object->SetPoint(points->first);
      object->SetPoint2(points->second);
    }
    XCAFDoc_Dimension::Set(dimension)->SetObject(object);
    gdt->SetDimension(m_fixture.top, m_fixture.bottom, dimension);
    return dimension;
  }

  // Number of PMI-blue pixels in a rectangle.
  static int blueIn(const QImage& image, const QRect& rect) {
    int count = 0;
    for (int y = std::max(0, rect.top()); y <= std::min(image.height() - 1, rect.bottom()); ++y) {
      for (int x = std::max(0, rect.left()); x <= std::min(image.width() - 1, rect.right()); ++x) {
        count += isPmiBlue(image.pixelColor(x, y)) ? 1 : 0;
      }
    }
    return count;
  }

private Q_SLOTS:
  void semanticLinearDimensionDrawsExtensionLinesAndArrows() {
    // Measures 100 mm along X on the top edge; the dimension line is 20 mm in front (y = -20).
    addSemanticDimension(XCAFDimTolObjects_DimensionType_Location_LinearDistance, gp_Pnt(50, -20, 20),
                         std::make_pair(gp_Pnt(0, 0, 20), gp_Pnt(100, 0, 20)), 100.0);
    load();
    QCOMPARE(m_pmiScene->arrowCount(), 2);
    const QImage image = render();
    sc::test::dumpImage(image, "pmi_semantic_linear_dimension");

    // Extension line at x = 0, halfway between the attachment point and the dimension line.
    const QPoint extension = pixelOf(0, -10, 20);
    QVERIFY2(blueIn(image, QRect(extension.x() - 1, extension.y() - 1, 3, 3)) >= 2, "no extension line");
    // Dimension line between the arrowheads, left of the label.
    const QPoint line = pixelOf(20, -20, 20);
    QVERIFY2(blueIn(image, QRect(line.x() - 1, line.y() - 1, 3, 3)) >= 2, "no dimension line");
    // Nothing between the extension lines off the dimension line.
    const QPoint empty = pixelOf(20, -12, 20);
    QCOMPARE(blueIn(image, QRect(empty.x() - 2, empty.y() - 2, 5, 5)), 0);

    // Arrowhead at the left end: a filled triangle pointing to -X, much denser than the bare
    // line the same-size window sees further along.
    const QPoint tip = pixelOf(0, -20, 20);
    const int arrow = blueIn(image, QRect(tip.x(), tip.y() - 4, 11, 9));
    const int bare = blueIn(image, QRect(line.x() - 5, line.y() - 4, 11, 9));
    QVERIFY2(arrow >= 25, qPrintable(QString("arrowhead has %1 pixels").arg(arrow)));
    QVERIFY2(arrow >= bare + 15, qPrintable(QString("arrow %1 vs line %2").arg(arrow).arg(bare)));
    // Its flanks are 3 px off the line near the base, and nothing lies beyond the tip.
    QVERIFY(blueIn(image, QRect(tip.x() + 9, tip.y() - 2, 2, 1)) >= 1);
    QCOMPARE(blueIn(image, QRect(tip.x() - 6, tip.y() - 3, 4, 7)), 0);
    // The right end has an arrowhead too.
    const QPoint tip2 = pixelOf(100, -20, 20);
    QVERIFY(blueIn(image, QRect(tip2.x() - 11, tip2.y() - 4, 11, 9)) >= 25);
  }

  void dimensionGeometryIsConstantInPixelsAndNotDrawnForFileGraphics() {
    // The fixture's only linear dimension has a presentation from the file.
    load();
    QCOMPARE(m_pmiScene->arrowCount(), 0);
    const QImage plain = render();
    m_pmiScene.reset(new sc::render::PmiScene(m_viewport->context()));
    addSemanticDimension(XCAFDimTolObjects_DimensionType_Location_LinearDistance, gp_Pnt(50, -20, 20),
                         std::make_pair(gp_Pnt(0, 0, 20), gp_Pnt(100, 0, 20)), 100.0);
    load();
    QCOMPARE(m_pmiScene->arrowCount(), 2);
    // The file-graphics item (the height dimension) adds nothing to the arrow count and
    // keeps drawing its presentation only.
    QVERIFY(m_pmiScene->presentationCount() > 0);

    // Zooming in does not grow the arrowheads.
    const QPoint tip = pixelOf(0, -20, 20);
    const int before = blueIn(render(), QRect(tip.x(), tip.y() - 4, 11, 9));
    m_viewport->view()->SetScale(m_viewport->view()->Scale() / 1.3);
    const QPoint zoomedTip = pixelOf(0, -20, 20);
    const QImage zoomed = render();
    const int after = blueIn(zoomed, QRect(zoomedTip.x(), zoomedTip.y() - 4, 11, 9));
    QVERIFY2(std::abs(after - before) <= 8, qPrintable(QString("%1 vs %2").arg(before).arg(after)));
    QVERIFY(before >= 25);
  }

  void semanticLabelSwitchHasAnAutomaticDefault() {
    load();
    QVERIFY(m_pmiScene->semanticLabelsVisible()); // 1 of 5 items has file graphics
    const PmiId flat = id(m_fixture.flatness);
    const auto& label = m_pmiScene->labelsOf(flat).front();
    QVERIFY(m_viewport->context()->IsDisplayed(label));
    m_pmiScene->setSemanticLabelsVisible(false);
    QVERIFY(!m_viewport->context()->IsDisplayed(label));
    // Highlighting still shows the targets while the labels are off.
    QVERIFY(m_pmiScene->labelsOf(id(m_fixture.height)).empty()); // file graphics: never a label
    m_pmiScene->highlight({flat});
    QVERIFY(countPixels(render(), isTargetOrange) > 50);
    m_pmiScene->setSemanticLabelsVisible(true);
    QVERIFY(m_viewport->context()->IsDisplayed(label));
    // Hiding the item while the labels are on and then switching keeps it hidden.
    m_pmiScene->setVisible({flat}, false);
    m_pmiScene->setSemanticLabelsVisible(false);
    m_pmiScene->setSemanticLabelsVisible(true);
    QVERIFY(!m_viewport->context()->IsDisplayed(label));
  }

  void semanticLabelsDefaultOffWhenMostItemsHaveGraphics() {
    // Four more items with a graphical presentation: 5 of 9 have file graphics.
    auto gdt = XCAFDoc_DocumentTool::DimTolTool(m_fixture.document->ocaf()->Main());
    for (int i = 0; i < 4; ++i) {
      const TDF_Label dimension = gdt->AddDimension();
      sc::OccHandle<XCAFDimTolObjects_DimensionObject> object = new XCAFDimTolObjects_DimensionObject();
      object->SetType(XCAFDimTolObjects_DimensionType_Location_LinearDistance);
      object->SetValue(10.0 + i);
      TopoDS_Compound lines;
      BRep_Builder builder;
      builder.MakeCompound(lines);
      builder.Add(lines,
                  BRepBuilderAPI_MakeEdge(gp_Pnt(5.0 * i, -5, 20), gp_Pnt(5.0 * i + 4, -5, 20)).Edge());
      object->SetPresentation(lines, new TCollection_HAsciiString("Extra"));
      XCAFDoc_Dimension::Set(dimension)->SetObject(object);
      gdt->SetDimension(m_fixture.top, m_fixture.bottom, dimension);
    }
    load();
    QVERIFY(!m_pmiScene->semanticLabelsVisible());
    const auto& label = m_pmiScene->labelsOf(id(m_fixture.flatness)).front();
    QVERIFY(!m_viewport->context()->IsDisplayed(label));
    m_pmiScene->setSemanticLabelsVisible(true);
    QVERIFY(m_viewport->context()->IsDisplayed(label));
  }

  void itemsWithoutSemanticsOrGraphicsDrawNothing() {
    load();
    QVERIFY(!m_fixture.label.IsNull());
    const PmiId note = id(m_fixture.label);
    QVERIFY(note != sc::core::kNoPmi);
    QVERIFY(m_pmiScene->labelsOf(note).empty());
  }

  void diameterGetsArrowedLeaderAndLinearWithoutPointsFallsBack() {
    // No attachment points: the target faces' centers stand in (top and bottom here, 20 apart).
    addSemanticDimension(XCAFDimTolObjects_DimensionType_Location_LinearDistance, gp_Pnt(50, -20, 20),
                         std::nullopt, 20.0);
    addSemanticDimension(XCAFDimTolObjects_DimensionType_Size_Diameter, gp_Pnt(50, 80, 20), std::nullopt,
                         8.0);
    load();
    // The linear one derives its points from the targets: its dimension line is along Z, which
    // is perpendicular to the plane normal's direction here, so it still gets two arrowheads;
    // the diameter one has a leader to the target with one arrow.
    QCOMPARE(m_pmiScene->arrowCount(), 3);
  }

  void nearbyAnchorsAreStackedWithoutOverlap() {
    // Two labels 1-2 mm apart (not identical) next to the flatness/parallelism labels; the
    // datum A label lies on the same screen spot as they do in this top view (20 mm below).
    const TDF_Label a = addSemanticDimension(XCAFDimTolObjects_DimensionType_Size_Diameter,
                                             gp_Pnt(51, 30, 20), std::nullopt, 8.0);
    const TDF_Label b = addSemanticDimension(XCAFDimTolObjects_DimensionType_Size_Diameter,
                                             gp_Pnt(52, 31, 20), std::nullopt, 9.0);
    load();
    m_pmiScene->declutter(m_viewport->view());
    const QImage image = render();
    sc::test::dumpImage(image, "pmi_nearby_labels");
    QVERIFY(id(a) != sc::core::kNoPmi && id(b) != sc::core::kNoPmi);
    std::vector<std::pair<PmiId, QRect>> boxes;
    for (const sc::core::PmiItem& item : m_pmi->items()) {
      for (const auto& label : m_pmiScene->labelsOf(item.id)) {
        const QPoint anchor = pixelOf(label->anchor().X(), label->anchor().Y(), label->anchor().Z());
        boxes.emplace_back(item.id, QRect(anchor.x() + int(label->boxLeft()),
                                          anchor.y() - int(label->centerY() + label->boxHeight() / 2.0),
                                          int(label->boxWidth()), int(label->boxHeight())));
      }
    }
    QVERIFY(boxes.size() >= 5);
    for (size_t i = 0; i < boxes.size(); ++i) {
      for (size_t j = i + 1; j < boxes.size(); ++j) {
        QVERIFY2(!boxes[i].second.intersects(boxes[j].second), "labels overlap");
      }
    }
    // Each box still picks its own PMI.
    for (const auto& [pmi, box] : boxes) {
      QCOMPARE(pmiAt(box.center()), pmi);
    }
  }

  void appliesSavedViews() {
    load();
    QCOMPARE(m_pmi->views().size(), size_t(1));
    m_pmiScene->applyView(m_pmi->views().front(), m_viewport->view());
    std::vector<PmiId> expected{id(m_fixture.flatness), id(m_fixture.height)};
    std::sort(expected.begin(), expected.end());
    QCOMPARE(m_pmiScene->visibleIds(), expected);
    const auto& camera = m_viewport->view()->Camera();
    QVERIFY(camera->Direction().IsEqual(gp_Dir(0, 1, 0), 1e-9));
    QVERIFY(camera->Up().IsEqual(gp_Dir(0, 0, 1), 1e-9));
    QVERIFY(camera->IsOrthographic());
    QCOMPARE(m_viewport->view()->ClipPlanes()->Size(), 1);
    sc::test::dumpImage(render(), "pmi_saved_view");

    m_pmiScene->clearViewClipping(m_viewport->view());
    QVERIFY(m_viewport->view()->ClipPlanes().IsNull() || m_viewport->view()->ClipPlanes()->Size() == 0);
  }

  // Saved views of the NIST models carry a camera (location, view window) in the STEP file. Applying
  // one must frame the part: inside the viewport, filling a good part of it.
  void nistSavedViewsFrameTheModel_data() {
    QTest::addColumn<QString>("file");
    QTest::addColumn<int>("viewIndex");
    QTest::newRow("ctc01-MBD_0") << "nist_ctc_01_asme1_ap242-e1.stp" << 0;
    QTest::newRow("ctc02-MBD_A") << "nist_ctc_02_asme1_ap242-e2.stp" << 0;
    QTest::newRow("ctc02-MBD_C") << "nist_ctc_02_asme1_ap242-e2.stp" << 2;
    QTest::newRow("ctc05-MBD_A") << "nist_ctc_05_asme1_ap242-e1.stp" << 0;
    QTest::newRow("ctc05-MBD_B") << "nist_ctc_05_asme1_ap242-e1.stp" << 1;
    QTest::newRow("ftc06-MBD_A") << "nist_ftc_06_asme1_ap242-e2.stp" << 0;
    QTest::newRow("ftc06-MBD_C") << "nist_ftc_06_asme1_ap242-e2.stp" << 2;
    QTest::newRow("ctc03-0") << "nist_ctc_03_asme1_ap242-e2.stp" << 0;
    QTest::newRow("ctc02-MBD_B") << "nist_ctc_02_asme1_ap242-e2.stp" << 1;
    QTest::newRow("ftc06-MBD_B") << "nist_ftc_06_asme1_ap242-e2.stp" << 1;
  }

  void nistSavedViewsFrameTheModel() {
    QFETCH(QString, file);
    QFETCH(int, viewIndex);
    const QByteArray dir = qgetenv("SC_TEST_DATA_DIR");
    const QString path = QString::fromUtf8(dir) + QLatin1Char('/') + file;
    if (dir.isEmpty() || !QFile::exists(path)) {
      QSKIP("NIST model not available (run tests/data/fetch.sh and set SC_TEST_DATA_DIR)");
    }
    const sc::core::ImportResult imported = sc::core::importStep(path.toStdString());
    QVERIFY2(imported.ok, imported.error.c_str());
    auto index = std::make_shared<sc::core::AssemblyIndex>(imported.document);
    m_scene->setModel(index);
    auto pmi = std::make_shared<PmiModel>(imported.document);
    m_pmiScene->setModel(pmi, index);
    QVERIFY(static_cast<size_t>(viewIndex) < pmi->views().size());
    const sc::core::PmiSavedView& saved = pmi->views()[static_cast<size_t>(viewIndex)];
    QVERIFY2(saved.eye.has_value(), "the STEP camera must have been matched to the view");

    m_pmiScene->applyView(saved, m_viewport->view());
    const QImage image = render();
    sc::test::dumpImage(image, QStringLiteral("nist_view_%1_%2")
                                   .arg(QString(QTest::currentDataTag()))
                                   .arg(viewIndex)
                                   .toLatin1()
                                   .constData());

    // The part's mesh, projected into the viewport (every few nodes is enough). A saved view may be
    // panned so that some of the part lies outside its window, so most of the part must be inside
    // the viewport and fill a reasonable share of it; CTC-01's frame must hold the whole part.
    int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
    int total = 0;
    int inside = 0;
    const int w = image.width();
    const int h = image.height();
    for (const sc::core::NodeId node : index->partInstances()) {
      for (TopExp_Explorer faces(index->worldShape(node), TopAbs_FACE); faces.More(); faces.Next()) {
        TopLoc_Location location;
        const auto mesh = BRep_Tool::Triangulation(TopoDS::Face(faces.Current()), location);
        if (mesh.IsNull()) {
          continue;
        }
        for (int i = 1; i <= mesh->NbNodes(); i += 7) {
          const gp_Pnt p = mesh->Node(i).Transformed(location);
          const QPoint px = pixelOf(p.X(), p.Y(), p.Z());
          minX = std::min(minX, px.x());
          maxX = std::max(maxX, px.x());
          minY = std::min(minY, px.y());
          maxY = std::max(maxY, px.y());
          ++total;
          inside += px.x() >= 0 && px.x() < w && px.y() >= 0 && px.y() < h ? 1 : 0;
        }
      }
    }
    QVERIFY(total > 100);
    qInfo() << QTest::currentDataTag() << "part pixels" << minX << minY << maxX << maxY << "in" << w << "x"
            << h << "inside" << double(inside) / total;
    const QRect visibleBox = QRect(QPoint(minX, minY), QPoint(maxX, maxY)).intersected(QRect(0, 0, w, h));
    QVERIFY2(double(inside) / total > 0.6, "most of the part lies outside the viewport");
    QVERIFY2(std::max(visibleBox.width() / double(w), visibleBox.height() / double(h)) > 0.2,
             "the part is too small in the viewport");
    if (file.startsWith(QLatin1String("nist_ctc_01"))) {
      QVERIFY2(minX >= 0 && minY >= 0 && maxX < w && maxY < h, "the whole part must be in the viewport");
    }
    QVERIFY2(std::max(maxX - minX, maxY - minY) < 1.5 * std::max(w, h),
             "the part is far larger than the view");
  }
};

int sc::test::runPmiSceneTests(int argc, char** argv) {
  PmiSceneTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_pmi_scene.moc"
