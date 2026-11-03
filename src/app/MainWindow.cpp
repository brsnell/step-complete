#include "app/MainWindow.h"

#include "app/AboutDialog.h"
#include "app/AssemblyTreeModel.h"
#include "app/ExportImageDialog.h"
#include "app/ImportJob.h"
#include "app/MeasurementsPanel.h"
#include "app/PmiPanel.h"
#include "app/PreferencesDialog.h"
#include "app/PropertiesPanel.h"
#include "app/SectionPanel.h"
#include "core/OcctInfo.h"
#include "core/Version.h"
#include "render/MeasureTool.h"
#include "render/OcctViewport.h"
#include "render/PmiScene.h"
#include "render/SceneManager.h"
#include "render/SceneStyle.h"
#include "render/SectionTool.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace sc::app {

namespace {

constexpr int kMaxRecentFiles = 10;
constexpr double kTransparency = 0.6;
const char* const kRecentFilesKey = "recentFiles";
const char* const kLastDirKey = "lastOpenDir";
const char* const kMeasureUnitKey = "measure/lengthUnit";
const char* const kMeasureDecimalsKey = "measure/decimals";
const char* const kMeasureAngleUnitKey = "measure/angleUnit";

bool isStepFile(const QString& path) {
  const QString suffix = QFileInfo(path).suffix().toLower();
  return suffix == QLatin1String("step") || suffix == QLatin1String("stp") ||
         suffix == QLatin1String("stpz") || suffix == QLatin1String("p21");
}

QString severityLabel(core::ImportMessage::Severity severity) {
  switch (severity) {
  case core::ImportMessage::Severity::Error:
    return QObject::tr("Error");
  case core::ImportMessage::Severity::Warning:
    return QObject::tr("Warning");
  default:
    return QObject::tr("Info");
  }
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(QString::fromUtf8(core::kAppName));
  resize(1280, 800);
  setAcceptDrops(true);

  m_viewport = new render::OcctViewport(this);
  setCentralWidget(m_viewport);
  m_scene = std::make_unique<render::SceneManager>(m_viewport->context());
  m_pmiScene = std::make_unique<render::PmiScene>(m_viewport->context());
  m_sectionTool = new render::SectionTool(m_viewport, m_scene.get(), this);
  // PMI labels are screen-space boxes: re-separate them once the camera stops moving.
  m_declutterTimer = new QTimer(this);
  m_declutterTimer->setSingleShot(true);
  m_declutterTimer->setInterval(150);
  connect(m_declutterTimer, &QTimer::timeout, this, [this] {
    if (m_pmi && !m_pmi->isEmpty() && m_pmiScene->semanticLabelsVisible()) {
      m_pmiScene->declutter(m_viewport->view());
      m_viewport->invalidateScene();
    }
  });
  m_viewport->installEventFilter(this);
  m_measureTool = new render::MeasureTool(m_viewport, m_scene.get(), this);
  connect(m_measureTool, &render::MeasureTool::measured, this, &MainWindow::onMeasured);
  connect(m_measureTool, &render::MeasureTool::pickProgress, this, &MainWindow::onPickProgress);
  connect(m_viewport, &render::OcctViewport::glInitialized, this, &MainWindow::onGlInitialized);
  connect(m_viewport, &render::OcctViewport::selectionChanged, this, &MainWindow::onViewportSelectionChanged);
  m_viewport->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_viewport, &QWidget::customContextMenuRequested, this,
          [this](const QPoint& pos) { showContextMenu(m_viewport, pos); });

  // Centered hint shown while no model is loaded.
  m_emptyHint = new QLabel(tr("Open a STEP file (%1) or drop one here")
                               .arg(QKeySequence(QKeySequence::Open).toString(QKeySequence::NativeText)),
                           m_viewport);
  m_emptyHint->setAlignment(Qt::AlignCenter);
  m_emptyHint->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_emptyHint->setStyleSheet(QStringLiteral("color: #3c4450; font-size: 15px; background: transparent;"));
  auto* hintLayout = new QVBoxLayout(m_viewport);
  hintLayout->addWidget(m_emptyHint);

  m_glStatus = new QLabel(this);
  m_unitStatus = new QLabel(this);
  statusBar()->addPermanentWidget(m_unitStatus);
  statusBar()->addPermanentWidget(m_glStatus);

  createDocks();
  createActions();
  updateActions();
}

MainWindow::~MainWindow() {
  delete m_import;      // cancels and waits for a running import
  delete m_measureTool; // removes its witness while the viewer exists
  // Presentations must be removed while the viewer still exists.
  delete m_sectionTool; // removes its handles and the parts' clip planes
  m_pmiScene.reset();
  m_scene.reset();
}

// ---------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------

void MainWindow::createDocks() {
  m_treeModel = new AssemblyTreeModel(this);
  m_treeView = new QTreeView(this);
  m_treeView->setModel(m_treeModel);
  m_treeView->setHeaderHidden(true);
  m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_treeView->setUniformRowHeights(true); // fast scrolling in large assemblies
  m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_treeView, &QWidget::customContextMenuRequested, this,
          [this](const QPoint& pos) { showContextMenu(m_treeView->viewport(), pos); });
  connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
          &MainWindow::onTreeSelectionChanged);
  connect(m_treeModel, &AssemblyTreeModel::visibilityRequested, this, &MainWindow::setNodeVisible);

  auto* treeDock = new QDockWidget(tr("Assembly"), this);
  treeDock->setObjectName(QStringLiteral("AssemblyDock"));
  treeDock->setWidget(m_treeView);
  addDockWidget(Qt::LeftDockWidgetArea, treeDock);

  m_properties = new PropertiesPanel(this);
  auto* propertiesDock = new QDockWidget(tr("Properties"), this);
  propertiesDock->setObjectName(QStringLiteral("PropertiesDock"));
  propertiesDock->setWidget(m_properties);
  addDockWidget(Qt::LeftDockWidgetArea, propertiesDock);
  splitDockWidget(treeDock, propertiesDock, Qt::Vertical);
  resizeDocks({treeDock}, {300}, Qt::Horizontal);

  m_messages = new QListWidget(this);
  m_messages->setAlternatingRowColors(true);
  m_messagesDock = new QDockWidget(tr("Import Messages"), this);
  m_messagesDock->setObjectName(QStringLiteral("MessagesDock"));
  m_messagesDock->setWidget(m_messages);
  addDockWidget(Qt::BottomDockWidgetArea, m_messagesDock);
  m_messagesDock->hide();

  m_pmiPanel = new PmiPanel(this);
  m_pmiDock = new QDockWidget(tr("PMI"), this);
  m_pmiDock->setObjectName(QStringLiteral("PmiDock"));
  m_pmiDock->setWidget(m_pmiPanel);
  addDockWidget(Qt::RightDockWidgetArea, m_pmiDock);
  m_pmiDock->hide();
  connect(m_pmiPanel, &PmiPanel::pmiSelected, this, &MainWindow::onPmiSelected);
  connect(m_pmiPanel, &PmiPanel::viewActivated, this, &MainWindow::activateSavedView);
  connect(m_pmiPanel, &PmiPanel::visibilityRequested, this,
          [this](const std::vector<core::PmiId>& ids, bool visible) {
            m_pmiScene->setVisible(ids, visible);
            m_viewport->invalidateScene();
          });

  m_sectionPanel = new SectionPanel(m_sectionTool, this);
  m_sectionDock = new QDockWidget(tr("Section"), this);
  m_sectionDock->setObjectName(QStringLiteral("SectionDock"));
  m_sectionDock->setWidget(m_sectionPanel);
  addDockWidget(Qt::RightDockWidgetArea, m_sectionDock);
  m_sectionDock->hide();
  connect(m_sectionPanel, &SectionPanel::sectionChanged, this, [this] {
    if (m_sectionAction != nullptr) {
      const QSignalBlocker blocker(m_sectionAction);
      m_sectionAction->setChecked(m_sectionTool->isActive());
    }
    m_viewport->invalidateScene();
  });

  m_measurements = new MeasurementsPanel(this);
  m_measurementsDock = new QDockWidget(tr("Measurements"), this);
  m_measurementsDock->setObjectName(QStringLiteral("MeasurementsDock"));
  m_measurementsDock->setWidget(m_measurements);
  addDockWidget(Qt::RightDockWidgetArea, m_measurementsDock);
  tabifyDockWidget(m_pmiDock, m_measurementsDock);
  tabifyDockWidget(m_pmiDock, m_sectionDock);
  resizeDocks({m_pmiDock}, {340}, Qt::Horizontal);
  m_measurementsDock->hide();
  connect(m_measurements, &MeasurementsPanel::currentChanged, this, [this](int index) {
    const auto& results = m_measurements->measurements();
    if (index >= 0 && index < static_cast<int>(results.size()) && results[static_cast<size_t>(index)].ok) {
      m_measureTool->showWitness(results[static_cast<size_t>(index)].witness);
    } else {
      m_measureTool->clearWitness();
    }
    m_viewport->invalidateScene();
  });
}

void MainWindow::createActions() {
  // --- File -----------------------------------------------------------------
  QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
  QAction* open = fileMenu->addAction(tr("&Open..."), this, &MainWindow::openFileDialog);
  open->setShortcut(QKeySequence::Open);
  m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));
  connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
  m_closeAction = fileMenu->addAction(tr("&Close"), this, &MainWindow::closeModel);
  m_closeAction->setShortcut(QKeySequence::Close);
  fileMenu->addSeparator();
  m_exportImageAction = fileMenu->addAction(tr("&Export Image..."), this, &MainWindow::exportImage);
  m_exportImageAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
  m_exportImageAction->setStatusTip(tr("Save the current 3D view as a PNG or JPEG image"));
  QAction* preferences = fileMenu->addAction(tr("&Preferences..."), this, &MainWindow::showPreferences);
  preferences->setMenuRole(QAction::PreferencesRole); // application menu on macOS
  fileMenu->addSeparator();
  QAction* quit = fileMenu->addAction(tr("&Quit"), qApp, &QApplication::quit);
  quit->setShortcut(QKeySequence::Quit);

  // --- View -----------------------------------------------------------------
  QMenu* viewMenu = menuBar()->addMenu(tr("&View"));

  QAction* fit = viewMenu->addAction(tr("&Fit All"), m_viewport, &render::OcctViewport::fitAll);
  fit->setShortcut(Qt::Key_F);
  m_fitSelectionAction = viewMenu->addAction(tr("Fit &Selection"), this, &MainWindow::fitSelection);
  m_fitSelectionAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F));

  QMenu* standardViews = viewMenu->addMenu(tr("Standard &Views"));
  struct NamedView {
    const char* label;
    V3d_TypeOfOrientation orientation;
    Qt::Key key;
  };
  const NamedView views[] = {
      {QT_TR_NOOP("&Front"), V3d_Yneg, Qt::Key_1},
      {QT_TR_NOOP("&Back"), V3d_Ypos, Qt::Key_2},
      {QT_TR_NOOP("&Top"), V3d_Zpos, Qt::Key_3},
      {QT_TR_NOOP("B&ottom"), V3d_Zneg, Qt::Key_4},
      {QT_TR_NOOP("&Left"), V3d_Xneg, Qt::Key_5},
      {QT_TR_NOOP("&Right"), V3d_Xpos, Qt::Key_6},
      {QT_TR_NOOP("&Isometric"), V3d_XposYnegZpos, Qt::Key_0},
  };
  for (const NamedView& v : views) {
    QAction* action = standardViews->addAction(tr(v.label));
    action->setShortcut(v.key);
    const V3d_TypeOfOrientation orientation = v.orientation;
    connect(action, &QAction::triggered, this,
            [this, orientation] { m_viewport->setStandardView(orientation); });
  }

  QAction* perspective = viewMenu->addAction(tr("&Perspective Projection"));
  perspective->setCheckable(true);
  perspective->setShortcut(Qt::Key_P);
  connect(perspective, &QAction::toggled, m_viewport, &render::OcctViewport::setPerspective);

  viewMenu->addSeparator();
  m_styleGroup = new QActionGroup(this);
  struct NamedStyle {
    const char* label;
    render::DisplayStyle style;
    Qt::Key key;
  };
  const NamedStyle styles[] = {
      {QT_TR_NOOP("S&haded"), render::DisplayStyle::Shaded, Qt::Key_S},
      {QT_TR_NOOP("Shaded with &Edges"), render::DisplayStyle::ShadedWithEdges, Qt::Key_E},
      {QT_TR_NOOP("&Wireframe"), render::DisplayStyle::Wireframe, Qt::Key_W},
  };
  for (const NamedStyle& s : styles) {
    QAction* action = viewMenu->addAction(tr(s.label));
    action->setCheckable(true);
    action->setShortcut(s.key);
    action->setChecked(s.style == m_scene->displayStyle());
    m_styleGroup->addAction(action);
    const render::DisplayStyle style = s.style;
    connect(action, &QAction::triggered, this, [this, style] {
      m_scene->setDisplayStyle(style);
      m_viewport->invalidateScene();
    });
  }

  viewMenu->addSeparator();
  m_hideAction = viewMenu->addAction(tr("&Hide"), this, &MainWindow::hideSelected);
  m_hideAction->setShortcut(Qt::Key_H);
  m_isolateAction = viewMenu->addAction(tr("&Isolate"), this, &MainWindow::isolateSelected);
  m_isolateAction->setShortcut(Qt::Key_I);
  m_showAllAction = viewMenu->addAction(tr("Show &All"), this, &MainWindow::showAll);
  m_showAllAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_H));
  m_transparentAction =
      viewMenu->addAction(tr("&Transparent"), this, &MainWindow::toggleTransparencySelected);
  m_transparentAction->setShortcut(Qt::Key_T);

  m_sectionAction = viewMenu->addAction(tr("Se&ction View"));
  m_sectionAction->setCheckable(true);
  m_sectionAction->setShortcut(Qt::Key_X);
  m_sectionAction->setStatusTip(
      tr("Cut the model with up to three planes (drag a plane's handle to move it)"));
  connect(m_sectionAction, &QAction::toggled, this, &MainWindow::setSectionView);
  m_showAllPmiAction = viewMenu->addAction(tr("Show All &PMI"), this, &MainWindow::showAllPmi);
  m_showAllPmiAction->setStatusTip(tr("Show every annotation and remove saved-view clipping"));
  m_semanticLabelsAction = viewMenu->addAction(tr("PMI &Labels from Semantics"));
  m_semanticLabelsAction->setCheckable(true);
  m_semanticLabelsAction->setStatusTip(
      tr("Draw annotations that have no graphics in the file as labels, dimension lines and arrows"));
  connect(m_semanticLabelsAction, &QAction::toggled, this, [this](bool on) {
    m_pmiScene->setSemanticLabelsVisible(on);
    m_declutterTimer->start();
    m_viewport->invalidateScene();
  });

  viewMenu->addSeparator();
  m_antialiasingAction = viewMenu->addAction(tr("&Antialiasing (4x MSAA)"));
  m_antialiasingAction->setCheckable(true);
  // Same setting as in the Preferences dialog; persisted in QSettings.
  m_antialiasingAction->setChecked(ViewPreferences::load().antialiasing);
  connect(m_antialiasingAction, &QAction::toggled, this, [this](bool on) {
    ViewPreferences preferences = ViewPreferences::load();
    preferences.antialiasing = on;
    preferences.save();
    applyViewPreferences(preferences);
  });
  QAction* frameRate = viewMenu->addAction(tr("Show Frame &Rate"));
  frameRate->setCheckable(true);
  connect(frameRate, &QAction::toggled, m_viewport, &render::OcctViewport::setShowFrameRate);

  viewMenu->addSeparator();
  QMenu* panels = viewMenu->addMenu(tr("&Panels"));
  for (QDockWidget* dock : findChildren<QDockWidget*>()) {
    panels->addAction(dock->toggleViewAction());
  }

  createMeasureMenu();
  m_measurements->setHint(tr("Choose a measurement from the Measure menu."));

  // --- Help -----------------------------------------------------------------
  QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
  helpMenu->addAction(tr("&Graphics Information..."), this, &MainWindow::showGraphicsInfo);
  helpMenu->addSeparator();
  helpMenu->addAction(tr("&About %1").arg(QString::fromUtf8(core::kAppName)), this, &MainWindow::showAbout);
  helpMenu->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

void MainWindow::createMeasureMenu() {
  QSettings settings;
  m_measureOptions.lengthUnit = static_cast<core::LengthUnit>(
      settings.value(kMeasureUnitKey, int(core::LengthUnit::Millimeter)).toInt());
  m_measureOptions.decimals = std::clamp(settings.value(kMeasureDecimalsKey, 3).toInt(), 0, 8);
  m_measureOptions.angleUnit = static_cast<core::AngleUnit>(
      settings.value(kMeasureAngleUnitKey, int(core::AngleUnit::Degree)).toInt());
  m_measureTool->setDisplayOptions(m_measureOptions);

  QMenu* menu = menuBar()->addMenu(tr("&Measure"));
  m_measureGroup = new QActionGroup(this);
  struct NamedMode {
    const char* label;
    core::MeasureMode mode;
    QKeySequence key;
    const char* tip;
  };
  const NamedMode modes[] = {
      {QT_TR_NOOP("Entity &Info"), core::MeasureMode::EntityInfo, QKeySequence(Qt::Key_M),
       QT_TR_NOOP("Type, size and position of a vertex, edge or face")},
      {QT_TR_NOOP("&Distance"), core::MeasureMode::Distance, QKeySequence(Qt::Key_D),
       QT_TR_NOOP("Minimum distance between two entities, plus center and parallel distances")},
      {QT_TR_NOOP("&Point to Point"), core::MeasureMode::PointToPoint, QKeySequence(Qt::SHIFT | Qt::Key_D),
       QT_TR_NOOP("Distance between two snapped points: vertices, edge midpoints, circle centers")},
      {QT_TR_NOOP("&Angle"), core::MeasureMode::Angle, QKeySequence(Qt::Key_A),
       QT_TR_NOOP("Angle between straight edges, planar faces or axes")},
      {QT_TR_NOOP("&Radius / Diameter"), core::MeasureMode::Radius, QKeySequence(Qt::Key_R),
       QT_TR_NOOP("Radius of a circular edge or a cylindrical, spherical or toroidal face")},
  };
  for (const NamedMode& m : modes) {
    QAction* action = menu->addAction(tr(m.label));
    action->setCheckable(true);
    action->setShortcut(m.key);
    action->setStatusTip(tr(m.tip));
    action->setData(int(m.mode));
    m_measureGroup->addAction(action);
    const int mode = int(m.mode);
    connect(action, &QAction::triggered, this, [this, mode] { setMeasureMode(mode); });
  }
  menu->addSeparator();
  m_massPropertiesAction = menu->addAction(tr("Mass &Properties"), this, [this] { measureMassProperties(); });
  m_massPropertiesAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_M));
  m_massPropertiesAction->setStatusTip(
      tr("Volume, surface area, centroid, inertia and bounding box of the selected parts (all visible "
         "parts when nothing is selected)"));
  menu->addSeparator();
  m_stopMeasureAction = menu->addAction(tr("&Stop Measuring"), this, [this] { setMeasureMode(-1); });
  m_stopMeasureAction->setShortcut(Qt::Key_Escape);

  menu->addSeparator();
  QMenu* units = menu->addMenu(tr("Length &Unit"));
  auto* unitGroup = new QActionGroup(this);
  for (const core::LengthUnit unit :
       {core::LengthUnit::Micrometer, core::LengthUnit::Millimeter, core::LengthUnit::Centimeter,
        core::LengthUnit::Meter, core::LengthUnit::Inch, core::LengthUnit::Foot}) {
    QAction* action = units->addAction(QString::fromUtf8(core::displayName(unit).data()));
    action->setCheckable(true);
    action->setChecked(unit == m_measureOptions.lengthUnit);
    unitGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, unit] {
      m_measureOptions.lengthUnit = unit;
      QSettings().setValue(kMeasureUnitKey, int(unit));
      applyMeasureOptions();
    });
  }
  QMenu* precision = menu->addMenu(tr("&Precision"));
  auto* precisionGroup = new QActionGroup(this);
  for (int decimals = 0; decimals <= 6; ++decimals) {
    QAction* action = precision->addAction(tr("%n decimal(s)", nullptr, decimals));
    action->setCheckable(true);
    action->setChecked(decimals == m_measureOptions.decimals);
    precisionGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, decimals] {
      m_measureOptions.decimals = decimals;
      QSettings().setValue(kMeasureDecimalsKey, decimals);
      applyMeasureOptions();
    });
  }
  QMenu* angles = menu->addMenu(tr("A&ngle Unit"));
  auto* angleGroup = new QActionGroup(this);
  const std::pair<core::AngleUnit, const char*> angleUnits[] = {
      {core::AngleUnit::Degree, QT_TR_NOOP("Degrees")}, {core::AngleUnit::Radian, QT_TR_NOOP("Radians")}};
  for (const auto& [unit, label] : angleUnits) {
    QAction* action = angles->addAction(tr(label));
    action->setCheckable(true);
    action->setChecked(unit == m_measureOptions.angleUnit);
    angleGroup->addAction(action);
    const core::AngleUnit angleUnit = unit;
    connect(action, &QAction::triggered, this, [this, angleUnit] {
      m_measureOptions.angleUnit = angleUnit;
      QSettings().setValue(kMeasureAngleUnitKey, int(angleUnit));
      applyMeasureOptions();
    });
  }
  menu->addSeparator();
  menu->addAction(m_measurementsDock->toggleViewAction());
}

void MainWindow::applyMeasureOptions() {
  // New results use the new units; existing results keep the text they were measured with.
  m_measureTool->setDisplayOptions(m_measureOptions);
  statusBar()->showMessage(tr("New measurements use %1, %n decimal(s).", nullptr, m_measureOptions.decimals)
                               .arg(QString::fromUtf8(core::displayName(m_measureOptions.lengthUnit).data())),
                           5000);
}

void MainWindow::setMeasureMode(int mode) {
  std::optional<core::MeasureMode> newMode =
      mode < 0 ? std::nullopt : std::optional<core::MeasureMode>(static_cast<core::MeasureMode>(mode));
  if (newMode && !m_scene->model()) {
    statusBar()->showMessage(tr("Open a model to measure."), 4000);
    newMode.reset();
  }
  for (QAction* action : m_measureGroup->actions()) {
    action->setChecked(newMode && action->data().toInt() == mode);
  }
  if (newMode) {
    // Measuring works on sub-shapes: drop the part selection first.
    m_syncingSelection = true;
    m_treeView->clearSelection();
    m_syncingSelection = false;
    m_measurementsDock->show();
  }
  m_measureTool->setMode(newMode);
  if (!newMode) {
    m_measurements->setHint(tr("Choose a measurement from the Measure menu."));
    statusBar()->clearMessage();
  }
  updateActions();
}

void MainWindow::onPickProgress(int picked, int required) {
  const auto mode = m_measureTool->mode();
  if (!mode) {
    return;
  }
  const QString name = QString::fromUtf8(core::toString(*mode));
  const QString hint = required == 1
                           ? tr("%1: click an entity. Esc stops measuring.").arg(name)
                           : tr("%1: pick %2 of %3. Click a pick again to undo it; Esc stops measuring.")
                                 .arg(name)
                                 .arg(picked + 1)
                                 .arg(required);
  m_measurements->setHint(hint);
  statusBar()->showMessage(hint);
}

void MainWindow::onMeasured(const core::Measurement& measurement) {
  m_measurements->addMeasurement(measurement);
  if (!measurement.ok) {
    statusBar()->showMessage(QString::fromStdString(measurement.error), 6000);
  }
}

// Mass properties are not a pick mode: they describe whole parts. The scope is
//  - the parts below the current tree/3D selection (assemblies expand to their parts), or
//  - every part instance when nothing is selected.
// While a pick mode is active the selection holds sub-shape picks and the tree selection is
// cleared, so that case also measures all parts rather than guessing from the picks. Hidden
// parts are always skipped: what you see is what gets measured.
void MainWindow::measureMassProperties() {
  const std::shared_ptr<const core::AssemblyIndex> index = m_scene->model();
  if (!index) {
    statusBar()->showMessage(tr("Open a model to measure."), 4000);
    return;
  }
  const std::vector<core::NodeId> selection =
      m_measureTool->isActive() ? std::vector<core::NodeId>{} : selectedNodes();

  std::vector<core::NodeId> parts;
  if (selection.empty()) {
    parts = index->partInstances();
  } else {
    for (const core::NodeId node : selection) {
      const std::vector<core::NodeId> below = index->partsBelow(node);
      parts.insert(parts.end(), below.begin(), below.end());
    }
    std::sort(parts.begin(), parts.end());
    parts.erase(std::unique(parts.begin(), parts.end()), parts.end());
  }
  parts.erase(
      std::remove_if(parts.begin(), parts.end(), [this](core::NodeId id) { return !m_scene->isVisible(id); }),
      parts.end());
  if (parts.empty()) {
    statusBar()->showMessage(selection.empty() ? tr("There are no visible parts to measure.")
                                               : tr("The selection has no visible parts."),
                             5000);
    return;
  }

  QApplication::setOverrideCursor(Qt::WaitCursor);
  std::vector<core::MassItem> items;
  items.reserve(parts.size());
  for (const core::NodeId id : parts) {
    core::MassItem item;
    item.worldShape = index->worldShape(id);
    item.name = index->displayPath(id);
    item.declared = index->validationProperties(id);
    item.location = index->node(id).location;
    items.push_back(std::move(item));
  }
  const core::Measurement result = core::computeMassProperties(items, m_measureOptions);
  QApplication::restoreOverrideCursor();

  m_measurementsDock->show();
  // Adding selects the new row, which draws its witness (see MeasurementsPanel::currentChanged).
  m_measurements->addMeasurement(result);
  if (!result.ok) {
    statusBar()->showMessage(QString::fromStdString(result.error), 6000);
  }
}

void MainWindow::updateActions() {
  const bool hasModel = m_scene->model() != nullptr;
  const bool hasSelection = hasModel && !selectedNodes().empty();
  m_closeAction->setEnabled(hasModel);
  m_hideAction->setEnabled(hasSelection);
  m_isolateAction->setEnabled(hasSelection);
  m_transparentAction->setEnabled(hasSelection);
  m_fitSelectionAction->setEnabled(hasSelection);
  m_showAllAction->setEnabled(hasModel);
  m_showAllPmiAction->setEnabled(m_pmi && !m_pmi->isEmpty());
  m_semanticLabelsAction->setEnabled(m_pmi && !m_pmi->isEmpty());
  m_sectionAction->setEnabled(hasModel);
  if (m_stopMeasureAction != nullptr) {
    m_stopMeasureAction->setEnabled(m_measureTool->isActive());
    m_massPropertiesAction->setEnabled(hasModel);
    for (QAction* action : m_measureGroup->actions()) {
      action->setEnabled(hasModel);
    }
  }
}

void MainWindow::onGlInitialized(bool ok, const QString& message) {
  if (!ok) {
    m_glStatus->setText(tr("3D view unavailable"));
    QString details = message;
#ifdef __linux__
    details += tr("\n\nTry starting with --software-gl to use software rendering.");
#endif
    QMessageBox::critical(this, tr("Graphics Error"), details);
    return;
  }
  m_glStatus->setText(message);
  m_glReady = true;
  applyViewPreferences(ViewPreferences::load());
  if (!m_pendingFile.isEmpty()) {
    const QString path = std::exchange(m_pendingFile, QString());
    openFile(path);
  }
}

// ---------------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------------

void MainWindow::openFileWhenReady(const QString& path) {
  if (m_glReady) {
    openFile(path);
  } else {
    m_pendingFile = path;
  }
}

void MainWindow::openFileDialog() {
  QSettings settings;
  const QString dir = settings.value(QLatin1String(kLastDirKey)).toString();
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Open STEP File"), dir, tr("STEP files (*.step *.stp *.stpz *.p21);;All files (*)"));
  if (!path.isEmpty()) {
    settings.setValue(QLatin1String(kLastDirKey), QFileInfo(path).absolutePath());
    openFile(path);
  }
}

void MainWindow::openFile(const QString& path) {
  if (m_import) {
    statusBar()->showMessage(tr("Another file is still loading."), 4000);
    return;
  }
  if (!QFileInfo::exists(path)) {
    QMessageBox::warning(this, tr("Open File"), tr("The file does not exist:\n%1").arg(path));
    return;
  }

  m_import = new ImportJob(QFileInfo(path).absoluteFilePath(), this);

  m_progress =
      new QProgressDialog(tr("Opening %1...").arg(QFileInfo(path).fileName()), tr("Cancel"), 0, 100, this);
  m_progress->setWindowModality(Qt::WindowModal);
  m_progress->setMinimumDuration(400);
  m_progress->setAutoClose(false);
  m_progress->setAutoReset(false);
  m_progress->setValue(0);
  connect(m_progress, &QProgressDialog::canceled, m_import, &ImportJob::cancel);
  connect(m_import, &ImportJob::progress, m_progress, [this, path](int percent, const QString& stage) {
    if (m_progress) {
      m_progress->setLabelText(tr("%1\n%2...").arg(QFileInfo(path).fileName(), stage));
      m_progress->setValue(std::min(percent, 99));
    }
  });
  connect(m_import, &ImportJob::finished, this, &MainWindow::onImportFinished);

  statusBar()->showMessage(tr("Loading %1...").arg(QFileInfo(path).fileName()));
  m_import->start();
}

void MainWindow::onImportFinished() {
  ImportJob* job = m_import;
  if (job == nullptr) {
    return;
  }
  if (m_progress) {
    m_progress->close();
    m_progress->deleteLater();
  }
  const core::ImportResult& result = job->result();
  const QString fileName = QFileInfo(job->path()).fileName();

  if (result.cancelled) {
    statusBar()->showMessage(tr("Opening %1 was cancelled.").arg(fileName), 5000);
  } else if (!result.ok || !job->index()) {
    statusBar()->clearMessage();
    QMessageBox::warning(this, tr("Open File"),
                         tr("Could not open %1.\n\n%2").arg(fileName, QString::fromStdString(result.error)));
  } else {
    // Replace the previous model only once the new one loaded successfully.
    std::shared_ptr<const core::AssemblyIndex> index = job->index();
    m_transparentParts.clear();
    m_measureTool->reset();
    m_pmiScene->clearViewClipping(m_viewport->view());
    m_sectionTool->reset();
    m_scene->setModel(index);
    m_sectionTool->setBounds(m_scene->boundingBox(index->roots()));
    m_sectionPanel->refresh();
    m_pmi = job->pmi();
    m_pmiScene->setModel(m_pmi, index, m_viewport->devicePixelRatioF());
    m_pmiPanel->setModel(m_pmi);
    {
      const QSignalBlocker blocker(m_semanticLabelsAction);
      m_semanticLabelsAction->setChecked(m_pmiScene->semanticLabelsVisible()); // auto default per file
    }
    m_declutterTimer->start();
    if (m_pmi && !m_pmi->isEmpty()) {
      m_pmiDock->show();
      m_pmiDock->raise();
    }
    render::SceneManager* scene = m_scene.get();
    m_treeModel->setModel(index, [scene](core::NodeId id) { return scene->isVisible(id); });
    m_treeView->expandToDepth(0);
    m_properties->setModel(index, result.stats, job->elapsedSeconds());

    m_messages->clear();
    int problems = 0;
    for (const core::ImportMessage& message : result.messages) {
      QString text =
          QStringLiteral("%1: %2").arg(severityLabel(message.severity), QString::fromStdString(message.text));
      if (message.count > 1) {
        text += tr(" (%1 times)").arg(message.count);
      }
      m_messages->addItem(text);
      problems += message.severity != core::ImportMessage::Severity::Info ? message.count : 0;
    }

    const auto& document = index->document();
    QString unit = QString::fromStdString(document->fileUnitName());
    if (document->fileUnit()) {
      unit = QString::fromUtf8(core::symbol(*document->fileUnit()).data());
    }
    m_unitStatus->setText(unit.isEmpty() ? QString() : tr("File unit: %1").arg(unit));

    m_emptyHint->hide();
    setWindowTitle(QStringLiteral("%1 — %2").arg(fileName, QString::fromUtf8(core::kAppName)));
    addRecentFile(job->path());
    m_viewport->fitAll();
    m_viewport->invalidateScene();

    QString summary = tr("Loaded %1: %2 part instances, %3 unique parts in %4 s.")
                          .arg(fileName)
                          .arg(index->partInstances().size())
                          .arg(index->uniquePartCount())
                          .arg(job->elapsedSeconds(), 0, 'f', 1);
    if (problems > 0) {
      summary += tr(" %1 translator messages (View → Panels → Import Messages).").arg(problems);
    }
    statusBar()->showMessage(summary, 15000);
  }

  job->deleteLater();
  m_import = nullptr;
  updateActions();
}

void MainWindow::closeModel() {
  setMeasureMode(-1);
  setSectionView(false);
  m_pmiScene->clearViewClipping(m_viewport->view());
  m_pmiScene->clear();
  m_pmiPanel->clear();
  m_pmi.reset();
  m_scene->setModel(nullptr);
  m_treeModel->setModel(nullptr, {});
  m_properties->clear();
  m_messages->clear();
  m_transparentParts.clear();
  m_unitStatus->clear();
  m_emptyHint->show();
  setWindowTitle(QString::fromUtf8(core::kAppName));
  m_viewport->invalidateScene();
  updateActions();
}

void MainWindow::addRecentFile(const QString& path) {
  QSettings settings;
  QStringList files = settings.value(QLatin1String(kRecentFilesKey)).toStringList();
  files.removeAll(path);
  files.prepend(path);
  while (files.size() > kMaxRecentFiles) {
    files.removeLast();
  }
  settings.setValue(QLatin1String(kRecentFilesKey), files);
}

void MainWindow::rebuildRecentMenu() {
  m_recentMenu->clear();
  const QStringList files = QSettings().value(QLatin1String(kRecentFilesKey)).toStringList();
  for (const QString& path : files) {
    QAction* action =
        m_recentMenu->addAction(QFileInfo(path).fileName(), this, [this, path] { openFile(path); });
    action->setToolTip(path);
    action->setStatusTip(path);
    action->setEnabled(QFileInfo::exists(path));
  }
  if (files.isEmpty()) {
    m_recentMenu->addAction(tr("No recent files"))->setEnabled(false);
  } else {
    m_recentMenu->addSeparator();
    m_recentMenu->addAction(tr("Clear Recent Files"), this,
                            [] { QSettings().remove(QLatin1String(kRecentFilesKey)); });
  }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == m_viewport) {
    switch (event->type()) {
    case QEvent::MouseButtonRelease:
    case QEvent::Wheel:
    case QEvent::Resize:
    case QEvent::KeyRelease: // standard views, fit
      m_declutterTimer->start();
      break;
    default:
      break;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
  const QList<QUrl> urls = event->mimeData()->urls();
  if (urls.size() == 1 && urls.front().isLocalFile() && isStepFile(urls.front().toLocalFile())) {
    event->acceptProposedAction();
  }
}

void MainWindow::dropEvent(QDropEvent* event) {
  const QList<QUrl> urls = event->mimeData()->urls();
  if (!urls.isEmpty()) {
    event->acceptProposedAction();
    openFile(urls.front().toLocalFile());
  }
}

// ---------------------------------------------------------------------------------
// Selection (tree <-> 3D)
// ---------------------------------------------------------------------------------

std::vector<core::NodeId> MainWindow::selectedNodes() const {
  std::vector<core::NodeId> nodes;
  for (const QModelIndex& index : m_treeView->selectionModel()->selectedRows()) {
    const core::NodeId id = m_treeModel->nodeId(index);
    if (id != core::kNoNode) {
      nodes.push_back(id);
    }
  }
  return nodes;
}

void MainWindow::onTreeSelectionChanged() {
  const std::vector<core::NodeId> nodes = selectedNodes();
  m_properties->showSelection(nodes);
  updateActions();
  if (m_syncingSelection) {
    return;
  }
  if (m_measureTool->isActive()) {
    return; // in measure mode the 3D selection holds picks, not parts
  }
  m_syncingSelection = true;
  if (!m_pmiScene->highlighted().empty()) {
    m_pmiScene->highlight({});
    m_pmiPanel->selectPmi({});
  }
  m_scene->select(nodes);
  m_viewport->invalidateScene();
  m_syncingSelection = false;
}

void MainWindow::onViewportSelectionChanged() {
  if (m_syncingSelection || m_measureTool->isActive()) {
    return;
  }
  m_syncingSelection = true;
  QItemSelection selection;
  QModelIndex first;
  for (const core::NodeId part : m_scene->selectedParts()) {
    const QModelIndex index = m_treeModel->indexOf(part);
    if (index.isValid()) {
      selection.select(index, index);
      if (!first.isValid()) {
        first = index;
      }
    }
  }
  m_treeView->selectionModel()->select(selection,
                                       QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  if (first.isValid()) {
    m_treeView->scrollTo(first); // expands collapsed parents
    m_treeView->selectionModel()->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
  }
  m_syncingSelection = false;

  // A picked annotation: select it in the PMI panel and show what it applies to.
  const std::vector<core::PmiId> pmi = m_pmiScene->selectedPmi();
  if (!pmi.empty()) {
    m_pmiPanel->selectPmi(pmi);
    m_pmiScene->highlight(pmi);
    m_properties->showPmi(m_pmi, pmi);
    m_viewport->invalidateScene();
  } else {
    if (!m_pmiScene->highlighted().empty()) {
      m_pmiScene->highlight({});
      m_pmiPanel->selectPmi({});
      m_viewport->invalidateScene();
    }
    m_properties->showSelection(selectedNodes());
  }
  updateActions();
}

// ---------------------------------------------------------------------------------
// View commands
// ---------------------------------------------------------------------------------

void MainWindow::afterVisibilityChange() {
  m_treeModel->refreshVisibility();
  m_viewport->invalidateScene();
  updateActions();
}

void MainWindow::setNodeVisible(core::NodeId node, bool visible) {
  m_scene->setVisible({node}, visible);
  afterVisibilityChange();
}

void MainWindow::hideSelected() {
  m_scene->setVisible(selectedNodes(), false);
  afterVisibilityChange();
}

void MainWindow::isolateSelected() {
  m_scene->isolate(selectedNodes());
  afterVisibilityChange();
}

void MainWindow::showAll() {
  m_scene->showAll();
  afterVisibilityChange();
}

void MainWindow::toggleTransparencySelected() {
  const std::vector<core::NodeId> nodes = selectedNodes();
  if (nodes.empty() || !m_scene->model()) {
    return;
  }
  // Toggle based on the first selected part: transparent -> opaque, otherwise -> transparent.
  const auto parts = m_scene->model()->partsBelow(nodes.front());
  const bool makeTransparent =
      parts.empty() || std::find(m_transparentParts.begin(), m_transparentParts.end(), parts.front()) ==
                           m_transparentParts.end();
  m_scene->setTransparency(nodes, makeTransparent ? kTransparency : 0.0);
  for (const core::NodeId node : nodes) {
    for (const core::NodeId part : m_scene->model()->partsBelow(node)) {
      m_transparentParts.erase(std::remove(m_transparentParts.begin(), m_transparentParts.end(), part),
                               m_transparentParts.end());
      if (makeTransparent) {
        m_transparentParts.push_back(part);
      }
    }
  }
  m_viewport->invalidateScene();
}

void MainWindow::fitSelection() {
  const Bnd_Box box = m_scene->boundingBox(selectedNodes());
  if (box.IsVoid()) {
    return;
  }
  m_viewport->view()->FitAll(box, 0.05, false);
  m_viewport->invalidateScene();
}

void MainWindow::showContextMenu(QWidget* source, const QPoint& pos) {
  if (!m_scene->model()) {
    return;
  }
  updateActions();
  QMenu menu(this);
  menu.addAction(m_hideAction);
  menu.addAction(m_isolateAction);
  menu.addAction(m_showAllAction);
  menu.addSeparator();
  menu.addAction(m_transparentAction);
  menu.addAction(m_fitSelectionAction);
  menu.exec(source->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------------

void MainWindow::setSectionView(bool on) {
  if (on && !m_scene->model()) {
    statusBar()->showMessage(tr("Open a model to cut it."), 4000);
    on = false;
  }
  if (on) {
    if (!m_sectionTool->isActive()) {
      render::SectionPlane first = m_sectionTool->plane(0);
      first.enabled = true;
      m_sectionTool->setPlane(0, first);
    }
    m_sectionDock->show();
    m_sectionDock->raise();
  } else {
    m_sectionTool->reset();
  }
  {
    const QSignalBlocker blocker(m_sectionAction);
    m_sectionAction->setChecked(on);
  }
  m_sectionPanel->refresh();
  m_viewport->invalidateScene();
}

// ---------------------------------------------------------------------------------
// PMI
// ---------------------------------------------------------------------------------

void MainWindow::onPmiSelected(const std::vector<core::PmiId>& ids) {
  if (m_measureTool->isActive()) {
    setMeasureMode(-1); // PMI selection replaces measurement picks
  }
  m_syncingSelection = true;
  m_treeView->clearSelection();
  m_syncingSelection = false;
  m_pmiScene->highlight(ids);
  if (ids.empty()) {
    m_properties->showSelection({});
  } else {
    m_properties->showPmi(m_pmi, ids);
  }
  m_viewport->invalidateScene();
  updateActions();
}

void MainWindow::activateSavedView(int viewId) {
  if (!m_pmi || viewId < 0 || viewId >= static_cast<int>(m_pmi->views().size())) {
    return;
  }
  const core::PmiSavedView& view = m_pmi->views()[static_cast<size_t>(viewId)];
  m_pmiScene->applyView(view, m_viewport->view());
  m_declutterTimer->start();
  m_pmiPanel->setVisible(m_pmiPanel->visibleIds(), false);
  m_pmiPanel->setVisible(view.pmi, true);
  m_showAllPmiAction->setEnabled(true);
  m_viewport->invalidateScene();
  QString message = tr("Saved view \"%1\": %n annotation(s)", nullptr, static_cast<int>(view.pmi.size()))
                        .arg(QString::fromStdString(view.name));
  if (!view.clippingPlanes.empty()) {
    message += tr(", %n clipping plane(s)", nullptr, static_cast<int>(view.clippingPlanes.size()));
  }
  statusBar()->showMessage(message + tr(". View → Show All PMI restores the full view."), 8000);
}

void MainWindow::showAllPmi() {
  m_pmiScene->clearViewClipping(m_viewport->view());
  if (m_pmi) {
    std::vector<core::PmiId> all;
    for (const core::PmiItem& item : m_pmi->items()) {
      all.push_back(item.id);
    }
    m_pmiScene->setVisible(all, true);
    m_pmiPanel->setVisible(all, true);
  }
  m_viewport->invalidateScene();
}

// ---------------------------------------------------------------------------------
// Image export and view preferences
// ---------------------------------------------------------------------------------

void MainWindow::exportImage() {
  if (!m_glReady) {
    return;
  }
  QSettings settings;
  const QString dirKey = QStringLiteral("export/directory");
  // The window title is "<file> — <app>" while a model is open.
  const QString suggested =
      m_closeAction->isEnabled()
          ? QFileInfo(windowTitle().section(QChar(0x2014), 0, 0).trimmed()).completeBaseName()
          : QStringLiteral("view");
  QString selectedFilter;
  QString path =
      QFileDialog::getSaveFileName(this, tr("Export Image"),
                                   settings.value(dirKey, QDir::homePath()).toString() + QLatin1Char('/') +
                                       suggested + QStringLiteral(".png"),
                                   tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg)"), &selectedFilter);
  if (path.isEmpty()) {
    return;
  }
  settings.setValue(dirKey, QFileInfo(path).absolutePath());
  const QString suffix = QFileInfo(path).suffix().toLower();
  const bool jpeg = suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg") ||
                    (suffix != QLatin1String("png") && selectedFilter.startsWith(QLatin1String("JPEG")));
  if (suffix.isEmpty() || (suffix != QLatin1String("png") && !jpeg)) {
    path += jpeg ? QStringLiteral(".jpg") : QStringLiteral(".png");
  }

  ExportImageDialog dialog(m_viewport->framebufferSize(), render::OcctViewport::kMaxExportSize, !jpeg, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  QApplication::setOverrideCursor(Qt::WaitCursor);
  const QSize size = dialog.imageSize();
  const QImage image = m_viewport->renderImage(size.width(), size.height(), dialog.background());
  QApplication::restoreOverrideCursor();

  const bool saved = !image.isNull() && (jpeg ? image.save(path, "JPEG", 92) : image.save(path, "PNG"));
  if (saved) {
    statusBar()->showMessage(tr("Saved %1 x %2 image to %3").arg(image.width()).arg(image.height()).arg(path),
                             8000);
  } else {
    QMessageBox::warning(this, tr("Export Image"),
                         image.isNull() ? tr("The 3D view could not be rendered at this size.")
                                        : tr("Could not write %1.").arg(path));
  }
}

void MainWindow::showPreferences() {
  PreferencesDialog dialog(ViewPreferences::load(), this);
  connect(&dialog, &PreferencesDialog::preferencesChanged, this,
          [this](const ViewPreferences& preferences) { applyViewPreferences(preferences); });
  if (dialog.exec() == QDialog::Accepted) {
    dialog.preferences().save();
  }
  applyViewPreferences(ViewPreferences::load()); // after Cancel: back to the saved values
}

void MainWindow::applyViewPreferences(const ViewPreferences& preferences) {
  m_viewport->setBackground(preferences.background);
  m_viewport->setLighting(preferences.lighting);
  m_viewport->setMsaaSamples(preferences.antialiasing ? 4 : 0);
  if (m_antialiasingAction != nullptr && m_antialiasingAction->isChecked() != preferences.antialiasing) {
    const QSignalBlocker blocker(m_antialiasingAction);
    m_antialiasingAction->setChecked(preferences.antialiasing);
  }
}

void MainWindow::showGraphicsInfo() {
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Graphics Information"));
  dialog.resize(560, 420);

  auto* text = new QPlainTextEdit(&dialog);
  text->setReadOnly(true);
  text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  const QString info = m_viewport->isGlReady()
                           ? m_viewport->glInfo()
                           : tr("OpenGL was not initialized; the 3D view is unavailable.");
  text->setPlainText(info);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  QPushButton* copy = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
  connect(copy, &QPushButton::clicked, &dialog, [info] { QGuiApplication::clipboard()->setText(info); });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto* layout = new QVBoxLayout(&dialog);
  layout->addWidget(text);
  layout->addWidget(buttons);
  dialog.exec();
}

void MainWindow::showAbout() {
  AboutDialog dialog(this);
  if (m_viewport != nullptr) {
    dialog.setGraphicsInfo(m_viewport->glInfo());
  }
  dialog.exec();
}

} // namespace sc::app
