#include "app/MainWindow.h"

#include "app/AboutDialog.h"
#include "app/AssemblyFilterProxy.h"
#include "app/AssemblyTreeModel.h"
#include "app/ClashDialog.h"
#include "app/ClashJob.h"
#include "app/ClashPanel.h"
#include "app/ExportGeometryDialog.h"
#include "app/ExportImageDialog.h"
#include "app/ExportJob.h"
#include "app/GeometryCheckJob.h"
#include "app/GeometryCheckPanel.h"
#include "app/ImportJob.h"
#include "app/MaterialDialog.h"
#include "app/MeasurementsPanel.h"
#include "app/PartsListDialog.h"
#include "app/PmiPanel.h"
#include "app/PreferencesDialog.h"
#include "app/PropertiesPanel.h"
#include "app/SavedViewsDialog.h"
#include "app/SectionPanel.h"
#include "app/ThicknessJob.h"
#include "app/ThicknessPanel.h"
#include "core/FileFormat.h"
#include "core/Memory.h"
#include "core/OcctInfo.h"
#include "core/Version.h"
#include "render/MeasureTool.h"
#include "render/OcctViewport.h"
#include "render/PmiScene.h"
#include "render/SceneManager.h"
#include "render/SceneStyle.h"
#include "render/SectionTool.h"
#include "render/ThicknessScene.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Graphic3d_Camera.hxx>
#include <StdSelect_ViewerSelector3d.hxx>
#include <V3d_View.hxx>

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
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>
#include <utility>

namespace sc::app {

namespace {

constexpr int MaxRecentFiles = 10;
constexpr double Transparency = 0.6;
const char* const RecentFilesKey = "recentFiles";
const char* const LastDirKey = "lastOpenDir";
const char* const MeasureUnitKey = "measure/lengthUnit";
const char* const MeasureDecimalsKey = "measure/decimals";
const char* const MeasureMassUnitKey = "measure/massUnit";
const char* const MeasureAngleUnitKey = "measure/angleUnit";

// A file the importer reads, by its name (the importer itself goes by the content).
bool isCadFile(const QString& path) {
  return core::formatFromExtension(path.toUtf8().toStdString()) != core::FileFormat::Unknown;
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
  setWindowTitle(QString::fromUtf8(core::AppName));
  resize(1280, 800);
  setAcceptDrops(true);

  _viewport = new render::OcctViewport(this);
  setCentralWidget(_viewport);
  _scene = std::make_unique<render::SceneManager>(_viewport->context());
  _pmiScene = std::make_unique<render::PmiScene>(_viewport->context());
  _thicknessScene = std::make_unique<render::ThicknessScene>(_viewport->context(), *_scene);
  _sectionTool = new render::SectionTool(_viewport, _scene.get(), this);
  // PMI labels are screen-space boxes: re-separate them once the camera stops moving.
  _declutterTimer = new QTimer(this);
  _declutterTimer->setSingleShot(true);
  _declutterTimer->setInterval(150);
  connect(_declutterTimer, &QTimer::timeout, this, &MainWindow::declutterPmi);
  _viewport->installEventFilter(this);
  _measureTool = new render::MeasureTool(_viewport, _scene.get(), this);
  connect(_measureTool, &render::MeasureTool::measured, this, &MainWindow::onMeasured);
  connect(_measureTool, &render::MeasureTool::pickProgress, this, &MainWindow::onPickProgress);
  connect(_viewport, &render::OcctViewport::glInitialized, this, &MainWindow::onGlInitialized);
  connect(_viewport, &render::OcctViewport::selectionChanged, this, &MainWindow::onViewportSelectionChanged);
  // Witnesses and PMI are sized in device pixels: rebuild them on a screen with other scaling.
  connect(_viewport, &render::OcctViewport::devicePixelRatioChanged, this,
          &MainWindow::onDevicePixelRatioChanged);
  _viewport->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(_viewport, &QWidget::customContextMenuRequested, this,
          [this](const QPoint& pos) { showContextMenu(_viewport, pos); });

  // Centered hint shown while no model is loaded.
  _emptyHint = new QLabel(tr("Open a STEP file (%1) or drop one here")
                              .arg(QKeySequence(QKeySequence::Open).toString(QKeySequence::NativeText)),
                          _viewport);
  _emptyHint->setAlignment(Qt::AlignCenter);
  _emptyHint->setAttribute(Qt::WA_TransparentForMouseEvents);
  _emptyHint->setStyleSheet(QStringLiteral("color: #3c4450; font-size: 15px; background: transparent;"));
  auto* hintLayout = new QVBoxLayout(_viewport);
  hintLayout->addWidget(_emptyHint);

  _glStatus = new QLabel(this);
  _unitStatus = new QLabel(this);
  statusBar()->addPermanentWidget(_unitStatus);
  statusBar()->addPermanentWidget(_glStatus);

  createDocks();
  createActions();
  updateActions();
}

MainWindow::~MainWindow() {
  delete _import;           // cancels and waits for a running import
  delete _geometryCheckJob; // likewise for a running geometry check
  delete _clashJob;         // and a running clash check
  delete _thicknessJob;     // and a running thickness measurement
  // The panel and its dock still emit while child widgets are torn down; nothing may reach
  // syncMeasurementWitnesses once the tool is gone.
  disconnect(_measurements, nullptr, this, nullptr);
  disconnect(_measurementsDock->toggleViewAction(), nullptr, this, nullptr);
  disconnect(_geometryCheck, nullptr, this, nullptr);
  disconnect(_geometryCheckDock->toggleViewAction(), nullptr, this, nullptr);
  disconnect(_clashPanel, nullptr, this, nullptr);
  disconnect(_clashDock->toggleViewAction(), nullptr, this, nullptr);
  disconnect(_thicknessPanel, nullptr, this, nullptr);
  disconnect(_viewport, &render::OcctViewport::devicePixelRatioChanged, this, nullptr);
  delete _measureTool; // removes its witnesses while the viewer exists
  // Presentations must be removed while the viewer still exists.
  delete _sectionTool; // removes its handles and the parts' clip planes
  _pmiScene.reset();
  _thicknessScene.reset(); // gives the parts their own presentations back
  _scene.reset();
}

// ---------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------

void MainWindow::createDocks() {
  _treeModel = new AssemblyTreeModel(this);
  _treeProxy = new AssemblyFilterProxy(this);
  _treeProxy->setSourceModel(_treeModel);
  _treeFilter = new QLineEdit(this);
  _treeFilter->setObjectName(QStringLiteral("assemblyFilterEdit"));
  _treeFilter->setPlaceholderText(tr("Filter parts\xE2\x80\xA6"));
  _treeFilter->setClearButtonEnabled(true);
  connect(_treeFilter, &QLineEdit::textChanged, this, &MainWindow::applyTreeFilter);
  _treeView = new QTreeView(this);
  _treeView->setObjectName(QStringLiteral("assemblyTreeView"));
  _treeView->setModel(_treeProxy);
  _treeView->setHeaderHidden(true);
  _treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
  _treeView->setUniformRowHeights(true); // fast scrolling in large assemblies
  _treeView->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(_treeView, &QWidget::customContextMenuRequested, this,
          [this](const QPoint& pos) { showContextMenu(_treeView->viewport(), pos); });
  connect(_treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
          &MainWindow::onTreeSelectionChanged);
  connect(_treeModel, &AssemblyTreeModel::visibilityRequested, this, &MainWindow::setNodeVisible);

  auto* treeDock = new QDockWidget(tr("Assembly"), this);
  treeDock->setObjectName(QStringLiteral("AssemblyDock"));
  auto* treePanel = new QWidget(treeDock);
  auto* treeLayout = new QVBoxLayout(treePanel);
  treeLayout->setContentsMargins(0, 0, 0, 0);
  treeLayout->setSpacing(2);
  treeLayout->addWidget(_treeFilter);
  treeLayout->addWidget(_treeView);
  treeDock->setWidget(treePanel);
  _treeDock = treeDock;
  addDockWidget(Qt::LeftDockWidgetArea, treeDock);

  _properties = new PropertiesPanel(this);
  _properties->setMaterialLookup([this](core::NodeId part) { return materialOf(part); });
  auto* propertiesDock = new QDockWidget(tr("Properties"), this);
  propertiesDock->setObjectName(QStringLiteral("PropertiesDock"));
  propertiesDock->setWidget(_properties);
  addDockWidget(Qt::LeftDockWidgetArea, propertiesDock);
  splitDockWidget(treeDock, propertiesDock, Qt::Vertical);
  resizeDocks({treeDock}, {300}, Qt::Horizontal);

  _messages = new QListWidget(this);
  _messages->setAlternatingRowColors(true);
  _messagesDock = new QDockWidget(tr("Import Messages"), this);
  _messagesDock->setObjectName(QStringLiteral("MessagesDock"));
  _messagesDock->setWidget(_messages);
  addDockWidget(Qt::BottomDockWidgetArea, _messagesDock);
  _messagesDock->hide();

  _pmiPanel = new PmiPanel(this);
  _pmiDock = new QDockWidget(tr("PMI"), this);
  _pmiDock->setObjectName(QStringLiteral("PmiDock"));
  _pmiDock->setWidget(_pmiPanel);
  addDockWidget(Qt::RightDockWidgetArea, _pmiDock);
  _pmiDock->hide();
  connect(_pmiPanel, &PmiPanel::pmiSelected, this, &MainWindow::onPmiSelected);
  connect(_pmiPanel, &PmiPanel::viewActivated, this, &MainWindow::activateSavedView);
  connect(_pmiPanel, &PmiPanel::visibilityRequested, this,
          [this](const std::vector<core::PmiId>& ids, bool visible) {
            _pmiScene->setVisible(ids, visible);
            _viewport->invalidateScene();
          });

  _sectionPanel = new SectionPanel(_sectionTool, this);
  _sectionDock = new QDockWidget(tr("Section"), this);
  _sectionDock->setObjectName(QStringLiteral("SectionDock"));
  _sectionDock->setWidget(_sectionPanel);
  addDockWidget(Qt::RightDockWidgetArea, _sectionDock);
  _sectionDock->hide();
  connect(_sectionPanel, &SectionPanel::sectionChanged, this, [this] {
    if (_sectionAction != nullptr) {
      const QSignalBlocker blocker(_sectionAction);
      _sectionAction->setChecked(_sectionTool->isActive());
    }
    _viewport->invalidateScene();
  });

  // Plane on a picked face (docs/designs/angled-section-planes.md). One tool owns clicks at a
  // time: a pick stops measuring, and starting a measurement cancels a pick (setMeasureMode).
  connect(_sectionTool, &render::SectionTool::pickStarted, this, [this](int index) {
    if (_measureTool->isActive()) {
      setMeasureMode(-1); // before the tool makes faces pickable
    }
    statusBar()->showMessage(tr("Click a flat face for plane %1 (Esc cancels).").arg(index + 1));
    updateActions();
  });
  connect(_sectionTool, &render::SectionTool::pickRejected, this, [this] {
    statusBar()->showMessage(
        tr("Pick a flat face for plane %1 (Esc cancels).").arg(_sectionTool->pickingPlane() + 1));
  });
  connect(_sectionTool, &render::SectionTool::pickEnded, this, [this](int index, bool picked) {
    if (picked) {
      statusBar()->showMessage(tr("Plane %1 is on the picked face.").arg(index + 1), 5000);
      if (_sectionAction != nullptr) {
        const QSignalBlocker blocker(_sectionAction);
        _sectionAction->setChecked(_sectionTool->isActive());
      }
    } else {
      statusBar()->clearMessage();
    }
    _viewport->invalidateScene();
    updateActions();
  });
  _cancelPickAction = new QAction(tr("Cancel Face Pick"), this);
  _cancelPickAction->setShortcut(Qt::Key_Escape); // never enabled together with Stop Measuring
  _cancelPickAction->setEnabled(false);
  connect(_cancelPickAction, &QAction::triggered, _sectionTool, &render::SectionTool::cancelPick);
  addAction(_cancelPickAction);

  _measurements = new MeasurementsPanel(this);
  _measurementsDock = new QDockWidget(tr("Measurements"), this);
  _measurementsDock->setObjectName(QStringLiteral("MeasurementsDock"));
  _measurementsDock->setWidget(_measurements);
  addDockWidget(Qt::RightDockWidgetArea, _measurementsDock);
  tabifyDockWidget(_pmiDock, _measurementsDock);
  tabifyDockWidget(_pmiDock, _sectionDock);
  resizeDocks({_pmiDock}, {340}, Qt::Horizontal);
  _measurementsDock->hide();
  connect(_measurements, &MeasurementsPanel::displayChanged, this, &MainWindow::syncMeasurementWitnesses);
  // Closing the panel hides the results' witnesses; reopening it shows them again.
  connect(_measurementsDock->toggleViewAction(), &QAction::toggled, this,
          &MainWindow::syncMeasurementWitnesses);

  _geometryCheck = new GeometryCheckPanel(this);
  _geometryCheckDock = new QDockWidget(tr("Geometry Check"), this);
  _geometryCheckDock->setObjectName(QStringLiteral("GeometryCheckDock"));
  _geometryCheckDock->setWidget(_geometryCheck);
  addDockWidget(Qt::BottomDockWidgetArea, _geometryCheckDock);
  tabifyDockWidget(_messagesDock, _geometryCheckDock);
  _geometryCheckDock->hide();
  connect(_geometryCheck, &GeometryCheckPanel::issueChosen, this,
          [this](int index) { showGeometryIssue(index, false); });
  connect(_geometryCheck, &GeometryCheckPanel::issueActivated, this,
          [this](int index) { showGeometryIssue(index, true); });
  connect(_geometryCheck, &GeometryCheckPanel::cancelRequested, this, [this] {
    if (_geometryCheckJob) {
      _geometryCheckJob->cancel();
    }
  });
  connect(_geometryCheck, &GeometryCheckPanel::rerunRequested, this, &MainWindow::checkGeometry);
  // The highlight belongs to the panel: closing it removes the highlight, reopening restores it.
  connect(_geometryCheckDock->toggleViewAction(), &QAction::toggled, this,
          [this] { showGeometryIssue(_geometryCheck->currentIndex(), false); });

  _clashPanel = new ClashPanel(this);
  _clashDock = new QDockWidget(tr("Clashes"), this);
  _clashDock->setObjectName(QStringLiteral("ClashDock"));
  _clashDock->setWidget(_clashPanel);
  addDockWidget(Qt::BottomDockWidgetArea, _clashDock);
  tabifyDockWidget(_messagesDock, _clashDock);
  _clashDock->hide();
  connect(_clashPanel, &ClashPanel::clashChosen, this, [this](int index) { showClash(index, false); });
  connect(_clashPanel, &ClashPanel::clashActivated, this, [this](int index) { showClash(index, true); });
  connect(_clashPanel, &ClashPanel::cancelRequested, this, [this] {
    if (_clashJob) {
      _clashJob->cancel();
    }
  });
  connect(_clashPanel, &ClashPanel::rerunRequested, this, [this] {
    if (_clashIndex && _clashIndex == _scene->model() && !_clashJob) {
      startClashJob(_clashParts, _clashOptions);
    }
  });
  // Both panels draw with the same red highlight: whichever is closed stops drawing it.
  connect(_clashDock->toggleViewAction(), &QAction::toggled, this,
          [this] { showClash(_clashPanel->currentIndex(), false); });

  _thicknessPanel = new ThicknessPanel(this);
  _thicknessDock = new QDockWidget(tr("Wall Thickness"), this);
  _thicknessDock->setObjectName(QStringLiteral("ThicknessDock"));
  _thicknessDock->setWidget(_thicknessPanel);
  addDockWidget(Qt::RightDockWidgetArea, _thicknessDock);
  tabifyDockWidget(_pmiDock, _thicknessDock);
  _thicknessDock->hide();
  connect(_thicknessPanel, &ThicknessPanel::cancelRequested, this, [this] {
    if (_thicknessJob) {
      _thicknessJob->cancel();
    }
  });
  connect(_thicknessPanel, &ThicknessPanel::limitsChanged, this, [this](double thin, double thick) {
    _thicknessScene->setLimits(thin, thick);
    _viewport->invalidateScene();
  });
  connect(_viewport, &render::OcctViewport::hoverUpdated, this, &MainWindow::onThicknessHover);
}

void MainWindow::declutterPmi() {
  if (_pmi && !_pmi->isEmpty() && _pmiScene->semanticLabelsVisible()) {
    _pmiScene->declutter(_viewport->view());
    _viewport->invalidateScene();
  }
}

void MainWindow::onDevicePixelRatioChanged() {
  syncMeasurementWitnesses();
  _pmiScene->setDevicePixelRatio(_viewport->devicePixelRatioF());
  _viewport->invalidateScene();
  declutterPmi(); // the rebuilt labels start from their base positions
}

// Draws every checked result's witness while the Measurements panel is open; the one the
// user is looking at is emphasized.
void MainWindow::syncMeasurementWitnesses() {
  const auto& results = _measurements->measurements();
  const int current = _measurements->currentIndex();
  const bool panelOpen = _measurementsDock->toggleViewAction()->isChecked(); // open, even if tabbed behind
  std::vector<render::MeasureTool::ShownWitness> shown;
  for (int i = 0; i < static_cast<int>(results.size()) && panelOpen; ++i) {
    if (_measurements->isShown(i)) {
      shown.push_back({_measurements->idAt(i), results[static_cast<size_t>(i)].witness, i == current});
    }
  }
  _measureTool->setWitnesses(shown);
  _viewport->invalidateScene();
}

void MainWindow::createActions() {
  // --- File -----------------------------------------------------------------
  QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
  QAction* open = fileMenu->addAction(tr("&Open..."), this, &MainWindow::openFileDialog);
  open->setShortcut(QKeySequence::Open);
  _recentMenu = fileMenu->addMenu(tr("Open &Recent"));
  connect(_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
  _closeAction = fileMenu->addAction(tr("&Close"), this, &MainWindow::closeModel);
  _closeAction->setShortcut(QKeySequence::Close);
  fileMenu->addSeparator();
  _exportImageAction = fileMenu->addAction(tr("&Export Image..."), this, &MainWindow::exportImage);
  _exportImageAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
  _exportImageAction->setStatusTip(tr("Save the current 3D view as a PNG or JPEG image"));
  _exportGeometryAction = fileMenu->addAction(tr("Export &Geometry..."), this, &MainWindow::exportGeometry);
  _exportGeometryAction->setStatusTip(tr("Save the visible or selected parts as STL, OBJ or glTF"));
  _exportPartsListAction =
      fileMenu->addAction(tr("Export &Parts List..."), this, &MainWindow::exportPartsList);
  _exportPartsListAction->setStatusTip(tr("Save a bill of materials (one row per part) as CSV"));
  QAction* preferences = fileMenu->addAction(tr("&Preferences..."), this, &MainWindow::showPreferences);
  preferences->setMenuRole(QAction::PreferencesRole); // application menu on macOS
  fileMenu->addSeparator();
  QAction* quit = fileMenu->addAction(tr("&Quit"), qApp, &QApplication::quit);
  quit->setShortcut(QKeySequence::Quit);

  // --- View -----------------------------------------------------------------
  QMenu* viewMenu = menuBar()->addMenu(tr("&View"));

  QAction* fit = viewMenu->addAction(tr("&Fit All"), _viewport, &render::OcctViewport::fitAll);
  fit->setShortcut(Qt::Key_F);
  _fitSelectionAction = viewMenu->addAction(tr("Fit &Selection"), this, &MainWindow::fitSelection);
  _fitSelectionAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F));
  QAction* findPart = viewMenu->addAction(tr("Fi&nd Part..."), this, &MainWindow::focusTreeFilter);
  findPart->setShortcut(QKeySequence::Find);

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
            [this, orientation] { _viewport->setStandardView(orientation); });
  }

  _perspectiveAction = viewMenu->addAction(tr("&Perspective Projection"));
  _perspectiveAction->setCheckable(true);
  _perspectiveAction->setShortcut(Qt::Key_P);
  connect(_perspectiveAction, &QAction::toggled, _viewport, &render::OcctViewport::setPerspective);

  _savedViewsMenu = viewMenu->addMenu(tr("Sa&ved Views"));
  connect(_savedViewsMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildSavedViewsMenu);
  rebuildSavedViewsMenu(); // so its actions (and their shortcuts) exist before it is first shown

  viewMenu->addSeparator();
  _styleGroup = new QActionGroup(this);
  struct NamedStyle {
    const char* label;
    render::DisplayStyle style;
    Qt::Key key;
  };
  const NamedStyle styles[] = {
      {QT_TR_NOOP("S&haded"), render::DisplayStyle::Shaded, Qt::Key_S},
      {QT_TR_NOOP("Shaded with &Edges"), render::DisplayStyle::ShadedWithEdges, Qt::Key_E},
      {QT_TR_NOOP("&Wireframe"), render::DisplayStyle::Wireframe, Qt::Key_W},
      {QT_TR_NOOP("Hidden &Line"), render::DisplayStyle::HiddenLine, Qt::Key_L},
  };
  for (const NamedStyle& namedStyle : styles) {
    QAction* action = viewMenu->addAction(tr(namedStyle.label));
    action->setCheckable(true);
    action->setShortcut(namedStyle.key);
    action->setChecked(namedStyle.style == _scene->displayStyle());
    _styleGroup->addAction(action);
    const render::DisplayStyle style = namedStyle.style;
    connect(action, &QAction::triggered, this, [this, style] {
      _scene->setDisplayStyle(style);
      _viewport->invalidateScene();
    });
  }

  viewMenu->addSeparator();
  _hideAction = viewMenu->addAction(tr("&Hide"), this, &MainWindow::hideSelected);
  _hideAction->setShortcut(Qt::Key_H);
  _isolateAction = viewMenu->addAction(tr("&Isolate"), this, &MainWindow::isolateSelected);
  _isolateAction->setShortcut(Qt::Key_I);
  _showAllAction = viewMenu->addAction(tr("Show &All"), this, &MainWindow::showAll);
  _showAllAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_H));
  _transparentAction = viewMenu->addAction(tr("&Transparent"), this, &MainWindow::toggleTransparencySelected);
  _transparentAction->setShortcut(Qt::Key_T);

  _sectionAction = viewMenu->addAction(tr("Se&ction View"));
  _sectionAction->setCheckable(true);
  _sectionAction->setShortcut(Qt::Key_X);
  _sectionAction->setStatusTip(
      tr("Cut the model with up to three planes (drag a plane's handle to move it)"));
  connect(_sectionAction, &QAction::toggled, this, &MainWindow::setSectionView);
  _showAllPmiAction = viewMenu->addAction(tr("Show All &PMI"), this, &MainWindow::showAllPmi);
  _showAllPmiAction->setStatusTip(tr("Show every annotation and remove saved-view clipping"));
  _semanticLabelsAction = viewMenu->addAction(tr("PMI &Labels from Semantics"));
  _semanticLabelsAction->setCheckable(true);
  _semanticLabelsAction->setStatusTip(
      tr("Draw annotations that have no graphics in the file as labels, dimension lines and arrows"));
  connect(_semanticLabelsAction, &QAction::toggled, this, [this](bool on) {
    _pmiScene->setSemanticLabelsVisible(on);
    _declutterTimer->start();
    _viewport->invalidateScene();
  });

  viewMenu->addSeparator();
  _antialiasingAction = viewMenu->addAction(tr("&Antialiasing (4x MSAA)"));
  _antialiasingAction->setCheckable(true);
  // Same setting as in the Preferences dialog; persisted in QSettings.
  _antialiasingAction->setChecked(ViewPreferences::load().antialiasing);
  connect(_antialiasingAction, &QAction::toggled, this, [this](bool on) {
    ViewPreferences preferences = ViewPreferences::load();
    preferences.antialiasing = on;
    preferences.save();
    applyViewPreferences(preferences);
  });
  QAction* frameRate = viewMenu->addAction(tr("Show Frame &Rate"));
  frameRate->setCheckable(true);
  connect(frameRate, &QAction::toggled, _viewport, &render::OcctViewport::setShowFrameRate);

  viewMenu->addSeparator();
  QMenu* panels = viewMenu->addMenu(tr("&Panels"));
  for (QDockWidget* dock : findChildren<QDockWidget*>()) {
    panels->addAction(dock->toggleViewAction());
  }

  createMeasureMenu();
  _measurements->setHint(tr("Choose a measurement from the Measure menu."));

  // --- Help -----------------------------------------------------------------
  QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
  helpMenu->addAction(tr("&Graphics Information..."), this, &MainWindow::showGraphicsInfo);
  helpMenu->addSeparator();
  helpMenu->addAction(tr("&About %1").arg(QString::fromUtf8(core::AppName)), this, &MainWindow::showAbout);
  helpMenu->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

void MainWindow::createMeasureMenu() {
  QSettings settings;
  _measureOptions.lengthUnit = static_cast<core::LengthUnit>(
      settings.value(MeasureUnitKey, int(core::LengthUnit::Millimeter)).toInt());
  _measureOptions.decimals = std::clamp(settings.value(MeasureDecimalsKey, 3).toInt(), 0, 8);
  _measureOptions.massUnit =
      static_cast<core::MassUnit>(settings.value(MeasureMassUnitKey, int(core::MassUnit::Kilogram)).toInt());
  _measureOptions.angleUnit =
      static_cast<core::AngleUnit>(settings.value(MeasureAngleUnitKey, int(core::AngleUnit::Degree)).toInt());
  _measureTool->setDisplayOptions(_measureOptions);

  QMenu* menu = menuBar()->addMenu(tr("&Measure"));
  _measureGroup = new QActionGroup(this);
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
  for (const NamedMode& namedMode : modes) {
    QAction* action = menu->addAction(tr(namedMode.label));
    action->setCheckable(true);
    action->setShortcut(namedMode.key);
    action->setStatusTip(tr(namedMode.tip));
    action->setData(int(namedMode.mode));
    _measureGroup->addAction(action);
    const int mode = int(namedMode.mode);
    connect(action, &QAction::triggered, this, [this, mode] { setMeasureMode(mode); });
  }
  menu->addSeparator();
  _massPropertiesAction = menu->addAction(tr("Mass &Properties"), this, [this] { measureMassProperties(); });
  _massPropertiesAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_M));
  _massPropertiesAction->setStatusTip(
      tr("Volume, surface area, centroid, inertia and bounding box of the selected parts (all visible "
         "parts when nothing is selected)"));
  _setMaterialAction = menu->addAction(tr("Set &Material..."), this, &MainWindow::setMaterialOfSelection);
  _setMaterialAction->setStatusTip(tr("Give the selected parts a material, for their mass"));
  _clearMaterialAction =
      menu->addAction(tr("C&lear Material"), this, [this] { setMaterial(selectedNodes(), std::nullopt); });
  _clearMaterialAction->setStatusTip(tr("Remove the material set on the selected parts"));
  menu->addSeparator();
  _checkGeometryAction = menu->addAction(tr("Check &Geometry"), this, &MainWindow::checkGeometry);
  _checkGeometryAction->setStatusTip(
      tr("Look for invalid geometry, open shells and tiny edges in the selected parts (all visible parts "
         "when nothing is selected)"));
  _checkClashesAction = menu->addAction(tr("Check &Clashes..."), this, &MainWindow::checkClashes);
  _thicknessAction = menu->addAction(tr("Wall &Thickness"));
  _thicknessAction->setCheckable(true);
  _thicknessAction->setStatusTip(
      tr("Color the selected parts (all visible parts when nothing is selected) by their wall thickness"));
  connect(_thicknessAction, &QAction::toggled, this, &MainWindow::showWallThickness);
  _checkClashesAction->setStatusTip(
      tr("Find parts that overlap, touch or come too close among the selected parts (all visible parts "
         "when nothing is selected)"));
  menu->addSeparator();
  _stopMeasureAction = menu->addAction(tr("&Stop Measuring"), this, [this] { setMeasureMode(-1); });
  _stopMeasureAction->setShortcut(Qt::Key_Escape);

  menu->addSeparator();
  QMenu* units = menu->addMenu(tr("Length &Unit"));
  auto* unitGroup = new QActionGroup(this);
  for (const core::LengthUnit unit :
       {core::LengthUnit::Micrometer, core::LengthUnit::Millimeter, core::LengthUnit::Centimeter,
        core::LengthUnit::Meter, core::LengthUnit::Inch, core::LengthUnit::Foot}) {
    QAction* action = units->addAction(QString::fromUtf8(core::displayName(unit).data()));
    action->setCheckable(true);
    action->setChecked(unit == _measureOptions.lengthUnit);
    unitGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, unit] {
      _measureOptions.lengthUnit = unit;
      QSettings().setValue(MeasureUnitKey, int(unit));
      applyMeasureOptions();
    });
  }
  QMenu* masses = menu->addMenu(tr("Mass Uni&t"));
  auto* massGroup = new QActionGroup(this);
  const std::pair<core::MassUnit, const char*> massUnits[] = {
      {core::MassUnit::Gram, QT_TR_NOOP("Grams")},
      {core::MassUnit::Kilogram, QT_TR_NOOP("Kilograms")},
      {core::MassUnit::Pound, QT_TR_NOOP("Pounds")}};
  for (const auto& [unit, name] : massUnits) {
    QAction* action = masses->addAction(tr(name));
    action->setCheckable(true);
    action->setChecked(unit == _measureOptions.massUnit);
    massGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, unit = unit] {
      _measureOptions.massUnit = unit;
      QSettings().setValue(MeasureMassUnitKey, int(unit));
      applyMeasureOptions();
    });
  }
  QMenu* precision = menu->addMenu(tr("&Precision"));
  auto* precisionGroup = new QActionGroup(this);
  for (int decimals = 0; decimals <= 6; ++decimals) {
    QAction* action = precision->addAction(tr("%n decimal(s)", nullptr, decimals));
    action->setCheckable(true);
    action->setChecked(decimals == _measureOptions.decimals);
    precisionGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, decimals] {
      _measureOptions.decimals = decimals;
      QSettings().setValue(MeasureDecimalsKey, decimals);
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
    action->setChecked(unit == _measureOptions.angleUnit);
    angleGroup->addAction(action);
    const core::AngleUnit angleUnit = unit;
    connect(action, &QAction::triggered, this, [this, angleUnit] {
      _measureOptions.angleUnit = angleUnit;
      QSettings().setValue(MeasureAngleUnitKey, int(angleUnit));
      applyMeasureOptions();
    });
  }
  menu->addSeparator();
  menu->addAction(_measurementsDock->toggleViewAction());
  menu->addAction(_geometryCheckDock->toggleViewAction());
  menu->addAction(_clashDock->toggleViewAction());
  menu->addAction(_thicknessDock->toggleViewAction());
}

void MainWindow::applyMeasureOptions() {
  // New results use the new units; existing results keep the text they were measured with.
  _measureTool->setDisplayOptions(_measureOptions);
  _thicknessScene->setUnit(_measureOptions.lengthUnit, _measureOptions.decimals);
  if (_thicknessScene->isShown()) {
    const double thin = _thicknessPanel->thinLimit();
    const double thick = _thicknessPanel->thickLimit();
    _thicknessPanel->setResult(*_thicknessScene->result(), _measureOptions);
    _thicknessPanel->setLimits(thin, thick);
    _viewport->invalidateScene();
  }
  statusBar()->showMessage(tr("New measurements use %1, %n decimal(s).", nullptr, _measureOptions.decimals)
                               .arg(QString::fromUtf8(core::displayName(_measureOptions.lengthUnit).data())),
                           5000);
}

void MainWindow::setMeasureMode(int mode) {
  std::optional<core::MeasureMode> newMode =
      mode < 0 ? std::nullopt : std::optional<core::MeasureMode>(static_cast<core::MeasureMode>(mode));
  if (newMode && !_scene->model()) {
    statusBar()->showMessage(tr("Open a model to measure."), 4000);
    newMode.reset();
  } else if (newMode && isMeshModel()) {
    statusBar()->showMessage(meshNotice(), 8000);
    newMode.reset();
  }
  for (QAction* action : _measureGroup->actions()) {
    action->setChecked(newMode && action->data().toInt() == mode);
  }
  if (newMode) {
    _sectionTool->cancelPick(); // one tool owns clicks at a time
    // Measuring picks faces, edges and vertices, which the thickness colors do not offer.
    if (_thicknessScene->isShown() || _thicknessJob) {
      stopThickness();
      statusBar()->showMessage(tr("Wall thickness view turned off to measure."), 4000);
    }
    // Measuring works on sub-shapes: drop the part selection first.
    _syncingSelection = true;
    _treeView->clearSelection();
    _syncingSelection = false;
    _measurementsDock->show();
  }
  _measureTool->setMode(newMode);
  if (!newMode) {
    _measurements->setHint(tr("Choose a measurement from the Measure menu."));
    statusBar()->clearMessage();
  }
  updateActions();
}

void MainWindow::onPickProgress(int picked, int required) {
  const auto mode = _measureTool->mode();
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
  _measurements->setHint(hint);
  statusBar()->showMessage(hint);
}

void MainWindow::onMeasured(const core::Measurement& measurement) {
  _measurementsDock->show(); // a closed panel hides witnesses: reopen it to show the new result
  _measurements->addMeasurement(measurement);
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
std::vector<core::NodeId> MainWindow::visiblePartsOf(const std::vector<core::NodeId>& selection) const {
  const std::shared_ptr<const core::AssemblyIndex>& index = _scene->model();
  std::vector<core::NodeId> parts;
  if (!index) {
    return parts;
  }
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
      std::remove_if(parts.begin(), parts.end(), [this](core::NodeId id) { return !_scene->isVisible(id); }),
      parts.end());
  return parts;
}

void MainWindow::checkGeometry() {
  const std::shared_ptr<const core::AssemblyIndex> index = _scene->model();
  if (!index) {
    statusBar()->showMessage(tr("Open a model to check its geometry."), 4000);
    return;
  }
  if (_geometryCheckJob) {
    return;
  }
  const std::vector<core::NodeId> selection =
      _measureTool->isActive() ? std::vector<core::NodeId>{} : selectedNodes();
  std::vector<core::NodeId> parts = visiblePartsOf(selection);
  if (parts.empty()) {
    statusBar()->showMessage(selection.empty() ? tr("There are no visible parts to check.")
                                               : tr("The selection has no visible parts."),
                             5000);
    return;
  }
  clearHighlight(Highlight::GeometryIssue);
  _checkedIndex.reset();
  _geometryCheckDock->show();
  _geometryCheckDock->raise();
  _geometryCheck->setRunning(0, QString());
  _geometryCheckJob = new GeometryCheckJob(index, std::move(parts), this);
  connect(_geometryCheckJob, &GeometryCheckJob::progress, _geometryCheck, &GeometryCheckPanel::setRunning);
  connect(_geometryCheckJob, &GeometryCheckJob::finished, this, &MainWindow::onGeometryCheckFinished);
  statusBar()->showMessage(tr("Checking geometry..."));
  _geometryCheckJob->start();
  updateActions();
  _viewport->invalidateScene();
}

bool MainWindow::isCheckingGeometry() const {
  return !_geometryCheckJob.isNull();
}

void MainWindow::onGeometryCheckFinished() {
  GeometryCheckJob* job = _geometryCheckJob;
  if (job == nullptr) {
    return;
  }
  const core::GeometryCheckResult& result = job->result();
  if (job->index() == _scene->model()) {
    _checkedIndex = job->index();
    _geometryCheck->setResult(result, *_checkedIndex);
    statusBar()->showMessage(GeometryCheckPanel::summaryOf(result), 10000);
  } else {
    _geometryCheck->clear(); // the model changed while the check ran
  }
  job->deleteLater();
  _geometryCheckJob = nullptr;
  updateActions();
}

void MainWindow::showGeometryIssue(int index, bool zoom) {
  const std::vector<core::GeometryIssue>& issues = _geometryCheck->issues();
  const bool panelOpen = _geometryCheckDock->toggleViewAction()->isChecked();
  if (!panelOpen || !_checkedIndex || _checkedIndex != _scene->model() || index < 0 ||
      index >= static_cast<int>(issues.size())) {
    clearHighlight(Highlight::GeometryIssue);
    _viewport->invalidateScene();
    return;
  }
  const core::GeometryIssue& issue = issues[static_cast<size_t>(index)];
  const TopoDS_Shape worldShape = issue.shape.Moved(_checkedIndex->node(issue.part).location);
  showHighlight(Highlight::GeometryIssue, worldShape);
  if (zoom) {
    Bnd_Box box;
    BRepBndLib::Add(worldShape, box);
    if (!box.IsVoid()) {
      // A tiny edge would fill the screen: show at least a little of the part around it.
      const Bnd_Box partBox = _scene->boundingBox({issue.part});
      if (!partBox.IsVoid()) {
        box.Enlarge(0.05 * std::sqrt(partBox.SquareExtent()));
      }
      _viewport->view()->FitAll(box, 0.05, false);
    }
  }
  _viewport->invalidateScene();
}

void MainWindow::stopGeometryCheck() {
  delete _geometryCheckJob; // cancels and waits; its result is not wanted
  _checkedIndex.reset();
  _geometryCheck->clear();
  clearHighlight(Highlight::GeometryIssue);
}

void MainWindow::checkClashes() {
  if (!_scene->model() || _clashJob) {
    return;
  }
  ClashDialog dialog(this);
  if (dialog.exec() == QDialog::Accepted) {
    checkClashesWith(dialog.options());
  }
}

bool MainWindow::isCheckingClashes() const {
  return !_clashJob.isNull();
}

void MainWindow::checkClashesWith(const core::ClashOptions& options) {
  if (!_scene->model()) {
    statusBar()->showMessage(tr("Open a model to check it for clashes."), 4000);
    return;
  }
  if (_clashJob) {
    return;
  }
  const std::vector<core::NodeId> selection =
      _measureTool->isActive() ? std::vector<core::NodeId>{} : selectedNodes();
  std::vector<core::NodeId> parts = visiblePartsOf(selection);
  if (parts.size() < 2) {
    statusBar()->showMessage(selection.empty()
                                 ? tr("A clash check needs at least two visible parts.")
                                 : tr("Select at least two visible parts to check for clashes."),
                             5000);
    return;
  }
  startClashJob(std::move(parts), options);
}

void MainWindow::startClashJob(std::vector<core::NodeId> parts, const core::ClashOptions& options) {
  const std::shared_ptr<const core::AssemblyIndex> index = _scene->model();
  clearHighlight(Highlight::Clash);
  _clashIndex.reset();
  _clashParts = parts;
  _clashOptions = options;
  _clashDock->show();
  _clashDock->raise();
  _clashPanel->setRunning(0, QString());
  _clashJob = new ClashJob(index, std::move(parts), options, this);
  connect(_clashJob, &ClashJob::progress, _clashPanel, &ClashPanel::setRunning);
  connect(_clashJob, &ClashJob::finished, this, &MainWindow::onClashCheckFinished);
  statusBar()->showMessage(tr("Checking clashes..."));
  _clashJob->start();
  updateActions();
  _viewport->invalidateScene();
}

void MainWindow::onClashCheckFinished() {
  ClashJob* job = _clashJob;
  if (job == nullptr) {
    return;
  }
  const core::ClashResult& result = job->result();
  if (job->index() == _scene->model()) {
    _clashIndex = job->index();
    _clashPanel->setResult(result, *_clashIndex, _measureOptions);
    statusBar()->showMessage(ClashPanel::summaryOf(result), 10000);
  } else {
    _clashPanel->clear(); // the model changed while the check ran
  }
  job->deleteLater();
  _clashJob = nullptr;
  updateActions();
}

void MainWindow::showClash(int index, bool zoom) {
  const std::vector<core::Clash>& clashes = _clashPanel->clashes();
  const bool panelOpen = _clashDock->toggleViewAction()->isChecked();
  if (!panelOpen || !_clashIndex || _clashIndex != _scene->model() || index < 0 ||
      index >= static_cast<int>(clashes.size())) {
    clearHighlight(Highlight::Clash);
    _viewport->invalidateScene();
    return;
  }
  const core::Clash& clash = clashes[static_cast<size_t>(index)];
  showHighlight(Highlight::Clash, clash.witness); // already in world coordinates
  if (!_measureTool->isActive() && !_sectionTool->isPicking()) {
    _scene->select({clash.a, clash.b});
    onViewportSelectionChanged(); // the tree and Properties follow
  }
  if (zoom) {
    Bnd_Box box = _scene->boundingBox({clash.a, clash.b});
    if (!box.IsVoid()) {
      _viewport->view()->FitAll(box, 0.05, false);
    }
  }
  _viewport->invalidateScene();
}

void MainWindow::showHighlight(Highlight owner, const TopoDS_Shape& worldShape) {
  _scene->showIssue(worldShape);
  _highlight = worldShape.IsNull() ? Highlight::None : owner;
}

void MainWindow::clearHighlight(Highlight owner) {
  if (_highlight == owner) {
    _scene->clearIssue();
    _highlight = Highlight::None;
  }
}

void MainWindow::stopClashCheck() {
  delete _clashJob; // cancels and waits; its result is not wanted
  _clashIndex.reset();
  _clashParts.clear();
  _clashPanel->clear();
  clearHighlight(Highlight::Clash);
}

bool MainWindow::isMeasuringThickness() const {
  return !_thicknessJob.isNull();
}

bool MainWindow::isThicknessShown() const {
  return _thicknessScene->isShown();
}

void MainWindow::showWallThickness(bool on) {
  if (_thicknessAction->isChecked() != on) {
    _thicknessAction->setChecked(on); // comes back here through toggled
    return;
  }
  if (!on) {
    stopThickness();
    _viewport->invalidateScene();
    return;
  }
  const std::shared_ptr<const core::AssemblyIndex> index = _scene->model();
  const std::vector<core::NodeId> selection =
      _measureTool->isActive() ? std::vector<core::NodeId>{} : selectedNodes();
  std::vector<core::NodeId> parts = visiblePartsOf(selection);
  if (!index || parts.empty()) {
    statusBar()->showMessage(!index              ? tr("Open a model to measure its wall thickness.")
                             : selection.empty() ? tr("There are no visible parts to measure.")
                                                 : tr("The selection has no visible parts."),
                             5000);
    const QSignalBlocker blocker(_thicknessAction);
    _thicknessAction->setChecked(false);
    return;
  }
  setMeasureMode(-1); // the colored parts cannot be picked for measuring
  _thicknessDock->show();
  _thicknessDock->raise();
  _thicknessPanel->setRunning(0, QString());
  _thicknessJob = new ThicknessJob(index, std::move(parts), this);
  connect(_thicknessJob, &ThicknessJob::progress, _thicknessPanel, &ThicknessPanel::setRunning);
  connect(_thicknessJob, &ThicknessJob::finished, this, &MainWindow::onThicknessFinished);
  statusBar()->showMessage(tr("Measuring wall thickness..."));
  _thicknessJob->start();
}

void MainWindow::onThicknessFinished() {
  ThicknessJob* job = _thicknessJob;
  if (job == nullptr) {
    return;
  }
  const std::shared_ptr<const core::ThicknessResult> result = job->result();
  job->deleteLater();
  _thicknessJob = nullptr;
  if (!result || job->index() != _scene->model()) {
    stopThickness();
    return;
  }
  _thicknessPanel->setResult(*result, _measureOptions);
  if (result->cancelled) {
    const QSignalBlocker blocker(_thicknessAction);
    _thicknessAction->setChecked(false);
    statusBar()->showMessage(_thicknessPanel->summary(), 5000);
    return;
  }
  _thicknessScene->setUnit(_measureOptions.lengthUnit, _measureOptions.decimals);
  _thicknessScene->show(result, *job->index());
  statusBar()->showMessage(_thicknessPanel->summary(), 10000);
  _viewport->invalidateScene();
}

// While the thickness view is on, the overlay marker shows the thickness under the cursor.
void MainWindow::onThicknessHover() {
  if (!_thicknessScene->isShown() || _measureTool->isActive()) {
    return;
  }
  const OccHandle<AIS_InteractiveContext>& context = _viewport->context();
  std::optional<double> thickness;
  gp_Pnt point;
  if (context->HasDetected()) {
    point = context->MainSelector()->PickedPoint(1);
    thickness = _thicknessScene->thicknessAt(context->DetectedInteractive(), point,
                                             _viewport->view()->Camera()->Direction());
  }
  if (thickness) {
    _viewport->setOverlayMarker(render::OcctViewport::OverlayMarker{
        point, QString::fromStdString(
                   core::formatLength(*thickness, _measureOptions.lengthUnit, _measureOptions.decimals))});
    _thicknessMarker = true;
  } else if (_thicknessMarker) {
    _viewport->setOverlayMarker(std::nullopt);
    _thicknessMarker = false;
  }
}

void MainWindow::stopThickness() {
  delete _thicknessJob; // cancels and waits; its result is not wanted
  _thicknessScene->clear();
  _thicknessPanel->clear();
  if (_thicknessMarker) {
    _viewport->setOverlayMarker(std::nullopt);
    _thicknessMarker = false;
  }
  const QSignalBlocker blocker(_thicknessAction);
  _thicknessAction->setChecked(false);
}

void MainWindow::measureMassProperties() {
  const std::shared_ptr<const core::AssemblyIndex> index = _scene->model();
  if (!index) {
    statusBar()->showMessage(tr("Open a model to measure."), 4000);
    return;
  }
  const std::vector<core::NodeId> selection =
      _measureTool->isActive() ? std::vector<core::NodeId>{} : selectedNodes();

  const std::vector<core::NodeId> parts = visiblePartsOf(selection);
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
    if (const std::optional<PropertiesPanel::PartMaterial> material = materialOf(id)) {
      item.density = material->material.density;
      item.material = material->material.name;
    }
    items.push_back(std::move(item));
  }
  const core::Measurement result = core::computeMassProperties(items, _measureOptions);
  QApplication::restoreOverrideCursor();

  _measurementsDock->show();
  // Adding emits MeasurementsPanel::displayChanged, so syncMeasurementWitnesses draws the witness.
  _measurements->addMeasurement(result);
  if (!result.ok) {
    statusBar()->showMessage(QString::fromStdString(result.error), 6000);
  }
}

bool MainWindow::isMeshModel() const {
  const std::shared_ptr<const core::AssemblyIndex>& index = _scene->model();
  return index && index->document() && core::isMeshFormat(index->document()->format());
}

QString MainWindow::meshNotice() const {
  const std::shared_ptr<const core::AssemblyIndex>& index = _scene->model();
  const QString format = index && index->document()
                             ? QString::fromLatin1(core::displayName(index->document()->format()))
                             : QString();
  return tr("Measuring exact geometry, Check Geometry and Check Clashes are off for mesh files (%1); Mass "
            "Properties and Wall Thickness use the triangles.")
      .arg(format);
}

void MainWindow::updateActions() {
  const bool hasModel = _scene->model() != nullptr;
  const bool hasSelection = hasModel && !selectedNodes().empty();
  _closeAction->setEnabled(hasModel);
  _exportGeometryAction->setEnabled(hasModel && !_export);
  _exportPartsListAction->setEnabled(hasModel);
  _hideAction->setEnabled(hasSelection);
  _isolateAction->setEnabled(hasSelection);
  _transparentAction->setEnabled(hasSelection);
  _fitSelectionAction->setEnabled(hasSelection);
  if (_setMaterialAction != nullptr) {
    _setMaterialAction->setEnabled(hasSelection);
    _clearMaterialAction->setEnabled(hasSelection);
  }
  _showAllAction->setEnabled(hasModel);
  _showAllPmiAction->setEnabled(_pmi && !_pmi->isEmpty());
  _semanticLabelsAction->setEnabled(_pmi && !_pmi->isEmpty());
  _sectionAction->setEnabled(hasModel);
  if (_cancelPickAction != nullptr) {
    _cancelPickAction->setEnabled(_sectionTool->isPicking());
  }
  if (_stopMeasureAction != nullptr) {
    _stopMeasureAction->setEnabled(_measureTool->isActive());
    // A mesh file (docs/designs/mesh-import.md) has no exact faces, edges or circles to pick.
    const bool exact = hasModel && !isMeshModel();
    _massPropertiesAction->setEnabled(hasModel);
    _checkGeometryAction->setEnabled(exact && !_geometryCheckJob);
    _checkClashesAction->setEnabled(exact && !_clashJob);
    _thicknessAction->setEnabled(hasModel);
    for (QAction* action : _measureGroup->actions()) {
      action->setEnabled(exact);
    }
  }
}

void MainWindow::onGlInitialized(bool ok, const QString& message) {
  if (!ok) {
    _glStatus->setText(tr("3D view unavailable"));
    QString details = message;
#ifdef __linux__
    details += tr("\n\nTry starting with --software-gl to use software rendering.");
#endif
    QMessageBox::critical(this, tr("Graphics Error"), details);
    return;
  }
  _glStatus->setText(message);
  _glReady = true;
  applyViewPreferences(ViewPreferences::load());
  if (!_pendingFile.isEmpty()) {
    const QString path = std::exchange(_pendingFile, QString());
    openFile(path);
  }
}

// ---------------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------------

void MainWindow::openFileWhenReady(const QString& path) {
  if (_glReady) {
    openFile(path);
  } else {
    _pendingFile = path;
  }
}

void MainWindow::openFileDialog() {
  QSettings settings;
  const QString dir = settings.value(QLatin1String(LastDirKey)).toString();
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Open Model"), dir,
      tr("Models (*.step *.stp *.stpz *.p21 *.igs *.iges *.stl *.obj *.gltf *.glb);;"
         "STEP files (*.step *.stp *.stpz *.p21);;IGES files (*.igs *.iges);;"
         "Mesh files (*.stl *.obj *.gltf *.glb);;All files (*)"));
  if (!path.isEmpty()) {
    settings.setValue(QLatin1String(LastDirKey), QFileInfo(path).absolutePath());
    openFile(path);
  }
}

void MainWindow::openFile(const QString& path) {
  if (_import) {
    // A newer request (Open, Recent, a Finder/Explorer double-click) wins: cancel the
    // current import and open this file once the cancelled one has wound down.
    _pendingFile = path;
    _import->cancel();
    return;
  }
  if (!QFileInfo::exists(path)) {
    QMessageBox::warning(this, tr("Open File"), tr("The file does not exist:\n%1").arg(path));
    return;
  }

  _import = new ImportJob(QFileInfo(path).absoluteFilePath(), this);

  _progress =
      new QProgressDialog(tr("Opening %1...").arg(QFileInfo(path).fileName()), tr("Cancel"), 0, 100, this);
  _progress->setWindowModality(Qt::WindowModal);
  _progress->setMinimumDuration(400);
  _progress->setAutoClose(false);
  _progress->setAutoReset(false);
  _progress->setValue(0);
  connect(_progress, &QProgressDialog::canceled, _import, &ImportJob::cancel);
  connect(_import, &ImportJob::progress, _progress, [this, path](int percent, const QString& stage) {
    if (_progress) {
      _progress->setLabelText(tr("%1\n%2...").arg(QFileInfo(path).fileName(), stage));
      _progress->setValue(std::min(percent, 99));
    }
  });
  connect(_import, &ImportJob::finished, this, &MainWindow::onImportFinished);

  statusBar()->showMessage(tr("Loading %1...").arg(QFileInfo(path).fileName()));
  _import->start();
}

void MainWindow::onImportFinished() {
  ImportJob* job = _import;
  if (job == nullptr) {
    return;
  }
  if (_progress) {
    _progress->close();
    _progress->deleteLater();
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
    _transparentParts.clear();
    stopGeometryCheck(); // problems belong to the previous model
    stopClashCheck();
    stopThickness();
    _measureTool->reset();
    _measurements->clear(); // results belong to the previous model
    _pmiScene->clearViewClipping(_viewport->view());
    _sectionTool->reset();
    _scene->setModel(index);
    _sectionTool->setBounds(_scene->boundingBox(index->roots()));
    _sectionPanel->refresh();
    _pmi = job->pmi();
    _pmiScene->setModel(_pmi, index, _viewport->devicePixelRatioF());
    _pmiPanel->setModel(_pmi);
    {
      const QSignalBlocker blocker(_semanticLabelsAction);
      _semanticLabelsAction->setChecked(_pmiScene->semanticLabelsVisible()); // auto default per file
    }
    _declutterTimer->start();
    if (_pmi && !_pmi->isEmpty()) {
      _pmiDock->show();
      _pmiDock->raise();
    }
    render::SceneManager* scene = _scene.get();
    _treeFilter->clear();
    _treeModel->setModel(index, [scene](core::NodeId id) { return scene->isVisible(id); });
    _treeView->expandToDepth(0);
    _properties->setModel(index, result.stats, job->elapsedSeconds());

    _messages->clear();
    int problems = 0;
    for (const core::ImportMessage& message : result.messages) {
      QString text =
          QStringLiteral("%1: %2").arg(severityLabel(message.severity), QString::fromStdString(message.text));
      if (message.count > 1) {
        text += tr(" (%1 times)").arg(message.count);
      }
      _messages->addItem(text);
      problems += message.severity != core::ImportMessage::Severity::Info ? message.count : 0;
    }

    const auto& document = index->document();
    QString unit = QString::fromStdString(document->fileUnitName());
    if (document->fileUnit()) {
      unit = QString::fromUtf8(core::symbol(*document->fileUnit()).data());
    }
    _unitStatus->setText(unit.isEmpty() ? QString() : tr("File unit: %1").arg(unit));

    _emptyHint->hide();
    setWindowTitle(QStringLiteral("%1 — %2").arg(fileName, QString::fromUtf8(core::AppName)));
    addRecentFile(job->path());
    loadModelData(job->path());
    _viewport->fitAll();
    _viewport->invalidateScene();

    QString summary = tr("Loaded %1: %2 part instances, %3 unique parts in %4 s.")
                          .arg(fileName)
                          .arg(index->partInstances().size())
                          .arg(index->uniquePartCount())
                          .arg(job->elapsedSeconds(), 0, 'f', 1);
    if (problems > 0) {
      summary += tr(" %1 translator messages (View → Panels → Import Messages).").arg(problems);
    }
    if (isMeshModel()) {
      summary += QLatin1Char(' ') + meshNotice();
    }
    statusBar()->showMessage(summary, 15000);
  }

  job->deleteLater();
  _import = nullptr;
  updateActions();
  core::releaseFreedMemory(); // the previous model, if one was replaced

  if (!_pendingFile.isEmpty() && _glReady) {
    openFile(std::exchange(_pendingFile, QString()));
  }
}

void MainWindow::closeModel() {
  stopGeometryCheck();
  stopClashCheck();
  stopThickness();
  setMeasureMode(-1);
  _measureTool->reset();
  _measurements->clear();
  setSectionView(false);
  _pmiScene->clearViewClipping(_viewport->view());
  _pmiScene->clear();
  _pmiPanel->clear();
  _pmi.reset();
  _scene->setModel(nullptr);
  _treeFilter->clear();
  _treeModel->setModel(nullptr, {});
  _properties->clear();
  _messages->clear();
  _transparentParts.clear();
  _unitStatus->clear();
  _modelPath.clear();
  _modelData = QJsonObject();
  _userViews.clear();
  _materials.clear();
  rebuildSavedViewsMenu();
  _emptyHint->show();
  setWindowTitle(QString::fromUtf8(core::AppName));
  _viewport->invalidateScene();
  updateActions();
  core::releaseFreedMemory();
}

void MainWindow::addRecentFile(const QString& path) {
  QSettings settings;
  QStringList files = settings.value(QLatin1String(RecentFilesKey)).toStringList();
  files.removeAll(path);
  files.prepend(path);
  while (files.size() > MaxRecentFiles) {
    files.removeLast();
  }
  settings.setValue(QLatin1String(RecentFilesKey), files);
}

void MainWindow::rebuildRecentMenu() {
  _recentMenu->clear();
  const QStringList files = QSettings().value(QLatin1String(RecentFilesKey)).toStringList();
  for (const QString& path : files) {
    QAction* action =
        _recentMenu->addAction(QFileInfo(path).fileName(), this, [this, path] { openFile(path); });
    action->setToolTip(path);
    action->setStatusTip(path);
    action->setEnabled(QFileInfo::exists(path));
  }
  if (files.isEmpty()) {
    _recentMenu->addAction(tr("No recent files"))->setEnabled(false);
  } else {
    _recentMenu->addSeparator();
    _recentMenu->addAction(tr("Clear Recent Files"), this,
                           [] { QSettings().remove(QLatin1String(RecentFilesKey)); });
  }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == _viewport) {
    switch (event->type()) {
    case QEvent::MouseButtonRelease:
    case QEvent::Wheel:
    case QEvent::Resize:
    case QEvent::KeyRelease: // standard views, fit
      _declutterTimer->start();
      break;
    default:
      break;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
  const QList<QUrl> urls = event->mimeData()->urls();
  if (urls.size() == 1 && urls.front().isLocalFile() && isCadFile(urls.front().toLocalFile())) {
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
  for (const QModelIndex& index : _treeView->selectionModel()->selectedRows()) {
    const core::NodeId id = _treeModel->nodeId(_treeProxy->mapToSource(index));
    if (id != core::NoNode) {
      nodes.push_back(id);
    }
  }
  return nodes;
}

void MainWindow::onTreeSelectionChanged() {
  const std::vector<core::NodeId> nodes = selectedNodes();
  _properties->showSelection(nodes);
  updateActions();
  if (_syncingSelection) {
    return;
  }
  if (_measureTool->isActive()) {
    return; // in measure mode the 3D selection holds picks, not parts
  }
  _syncingSelection = true;
  if (!_pmiScene->highlighted().empty()) {
    _pmiScene->highlight({});
    _pmiPanel->selectPmi({});
  }
  _scene->select(nodes);
  _viewport->invalidateScene();
  _syncingSelection = false;
}

void MainWindow::applyTreeFilter() {
  _treeProxy->setFilterText(_treeFilter->text());
  if (_treeProxy->filtering()) {
    expandToMatches({});
    return;
  }
  // Back to the unfiltered tree: the usual first level, plus the path to the selection.
  _treeView->collapseAll();
  _treeView->expandToDepth(0);
  const QModelIndexList selected = _treeView->selectionModel()->selectedRows();
  if (!selected.isEmpty()) {
    _treeView->scrollTo(selected.first());
  }
}

void MainWindow::focusTreeFilter() {
  _treeDock->show();
  _treeDock->raise();
  _treeFilter->setFocus(Qt::ShortcutFocusReason);
  _treeFilter->selectAll();
}

bool MainWindow::expandToMatches(const QModelIndex& parent) {
  bool found = false;
  for (int row = 0; row < _treeProxy->rowCount(parent); ++row) {
    const QModelIndex index = _treeProxy->index(row, 0, parent);
    const bool matchBelow = expandToMatches(index);
    _treeView->setExpanded(index, matchBelow);
    found = found || matchBelow || _treeProxy->matches(_treeProxy->mapToSource(index));
  }
  return found;
}

void MainWindow::onViewportSelectionChanged() {
  if (_syncingSelection || _measureTool->isActive() || _sectionTool->isPicking()) {
    return;
  }
  _syncingSelection = true;
  QItemSelection selection;
  QModelIndex first;
  for (const core::NodeId part : _scene->selectedParts()) {
    // A part the filter hides stays selected in 3D but has no row to select.
    const QModelIndex index = _treeProxy->mapFromSource(_treeModel->indexOf(part));
    if (index.isValid()) {
      selection.select(index, index);
      if (!first.isValid()) {
        first = index;
      }
    }
  }
  _treeView->selectionModel()->select(selection,
                                      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  if (first.isValid()) {
    _treeView->scrollTo(first); // expands collapsed parents
    _treeView->selectionModel()->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
  }
  _syncingSelection = false;

  // A picked annotation: select it in the PMI panel and show what it applies to.
  const std::vector<core::PmiId> pmi = _pmiScene->selectedPmi();
  if (!pmi.empty()) {
    _pmiPanel->selectPmi(pmi);
    _pmiScene->highlight(pmi);
    _properties->showPmi(_pmi, pmi);
    _viewport->invalidateScene();
  } else {
    if (!_pmiScene->highlighted().empty()) {
      _pmiScene->highlight({});
      _pmiPanel->selectPmi({});
      _viewport->invalidateScene();
    }
    _properties->showSelection(selectedNodes());
  }
  updateActions();
}

// ---------------------------------------------------------------------------------
// View commands
// ---------------------------------------------------------------------------------

void MainWindow::afterVisibilityChange() {
  _treeModel->refreshVisibility();
  _viewport->invalidateScene();
  updateActions();
}

void MainWindow::setNodeVisible(core::NodeId node, bool visible) {
  _scene->setVisible({node}, visible);
  afterVisibilityChange();
}

void MainWindow::hideSelected() {
  _scene->setVisible(selectedNodes(), false);
  afterVisibilityChange();
}

void MainWindow::isolateSelected() {
  _scene->isolate(selectedNodes());
  afterVisibilityChange();
}

void MainWindow::showAll() {
  _scene->showAll();
  afterVisibilityChange();
}

void MainWindow::toggleTransparencySelected() {
  const std::vector<core::NodeId> nodes = selectedNodes();
  if (nodes.empty() || !_scene->model()) {
    return;
  }
  // Toggle based on the first selected part: transparent -> opaque, otherwise -> transparent.
  const auto parts = _scene->model()->partsBelow(nodes.front());
  const bool makeTransparent = parts.empty() || std::find(_transparentParts.begin(), _transparentParts.end(),
                                                          parts.front()) == _transparentParts.end();
  _scene->setTransparency(nodes, makeTransparent ? Transparency : 0.0);
  for (const core::NodeId node : nodes) {
    for (const core::NodeId part : _scene->model()->partsBelow(node)) {
      _transparentParts.erase(std::remove(_transparentParts.begin(), _transparentParts.end(), part),
                              _transparentParts.end());
      if (makeTransparent) {
        _transparentParts.push_back(part);
      }
    }
  }
  _viewport->invalidateScene();
}

void MainWindow::fitSelection() {
  const Bnd_Box box = _scene->boundingBox(selectedNodes());
  if (box.IsVoid()) {
    return;
  }
  _viewport->view()->FitAll(box, 0.05, false);
  _viewport->invalidateScene();
}

void MainWindow::showContextMenu(QWidget* source, const QPoint& pos) {
  if (!_scene->model()) {
    return;
  }
  updateActions();
  QMenu menu(this);
  menu.addAction(_hideAction);
  menu.addAction(_isolateAction);
  menu.addAction(_showAllAction);
  menu.addSeparator();
  menu.addAction(_transparentAction);
  menu.addAction(_fitSelectionAction);
  menu.exec(source->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------------

void MainWindow::setSectionView(bool on) {
  if (on && !_scene->model()) {
    statusBar()->showMessage(tr("Open a model to cut it."), 4000);
    on = false;
  }
  if (on) {
    if (!_sectionTool->isActive()) {
      render::SectionPlane first = _sectionTool->plane(0);
      first.enabled = true;
      _sectionTool->setPlane(0, first);
    }
    _sectionDock->show();
    _sectionDock->raise();
  } else {
    _sectionTool->reset();
  }
  {
    const QSignalBlocker blocker(_sectionAction);
    _sectionAction->setChecked(on);
  }
  _sectionPanel->refresh();
  _viewport->invalidateScene();
}

// ---------------------------------------------------------------------------------
// PMI
// ---------------------------------------------------------------------------------

void MainWindow::onPmiSelected(const std::vector<core::PmiId>& ids) {
  if (_measureTool->isActive()) {
    setMeasureMode(-1); // PMI selection replaces measurement picks
  }
  _syncingSelection = true;
  _treeView->clearSelection();
  _syncingSelection = false;
  _pmiScene->highlight(ids);
  if (ids.empty()) {
    _properties->showSelection({});
  } else {
    _properties->showPmi(_pmi, ids);
  }
  _viewport->invalidateScene();
  updateActions();
}

void MainWindow::activateSavedView(int viewId) {
  if (!_pmi || viewId < 0 || viewId >= static_cast<int>(_pmi->views().size())) {
    return;
  }
  const core::PmiSavedView& view = _pmi->views()[static_cast<size_t>(viewId)];
  _pmiScene->applyView(view, _viewport->view());
  _declutterTimer->start();
  _pmiPanel->setVisible(_pmiPanel->visibleIds(), false);
  _pmiPanel->setVisible(view.pmi, true);
  _showAllPmiAction->setEnabled(true);
  _viewport->invalidateScene();
  QString message = tr("Saved view \"%1\": %n annotation(s)", nullptr, static_cast<int>(view.pmi.size()))
                        .arg(QString::fromStdString(view.name));
  if (!view.clippingPlanes.empty()) {
    message += tr(", %n clipping plane(s)", nullptr, static_cast<int>(view.clippingPlanes.size()));
  }
  statusBar()->showMessage(message + tr(". View → Show All PMI restores the full view."), 8000);
}

void MainWindow::showAllPmi() {
  _pmiScene->clearViewClipping(_viewport->view());
  if (_pmi) {
    std::vector<core::PmiId> all;
    for (const core::PmiItem& item : _pmi->items()) {
      all.push_back(item.id);
    }
    _pmiScene->setVisible(all, true);
    _pmiPanel->setVisible(all, true);
  }
  _viewport->invalidateScene();
}

// ---------------------------------------------------------------------------------
// Image export and view preferences
// ---------------------------------------------------------------------------------

void MainWindow::exportImage() {
  if (!_glReady) {
    return;
  }
  QSettings settings;
  const QString dirKey = QStringLiteral("export/directory");
  // The window title is "<file> — <app>" while a model is open.
  const QString suggested =
      _closeAction->isEnabled()
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

  ExportImageDialog dialog(_viewport->framebufferSize(), render::OcctViewport::MaxExportSize, !jpeg, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  QApplication::setOverrideCursor(Qt::WaitCursor);
  const QSize size = dialog.imageSize();
  const QImage image = _viewport->renderImage(size.width(), size.height(), dialog.background());
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

// ---------------------------------------------------------------------------------
// Geometry export (docs/designs/geometry-export.md)
// ---------------------------------------------------------------------------------

namespace {

QString geometryFileFilter(core::GeometryFormat format) {
  switch (format) {
  case core::GeometryFormat::StlMerged:
  case core::GeometryFormat::StlPerPart:
    return MainWindow::tr("STL files (*.stl)");
  case core::GeometryFormat::Obj:
    return MainWindow::tr("OBJ files (*.obj)");
  case core::GeometryFormat::Gltf:
    return MainWindow::tr("glTF files (*.gltf)");
  case core::GeometryFormat::Glb:
    return MainWindow::tr("GLB files (*.glb)");
  }
  return {};
}

} // namespace

void MainWindow::exportGeometry() {
  const std::shared_ptr<const core::AssemblyIndex>& index = _scene->model();
  if (!index || _export) {
    return;
  }
  ExportGeometryDialog dialog(!_scene->selectedParts().empty(), this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const core::GeometryExportOptions options = dialog.options();

  QSettings settings;
  const QString dirKey = QStringLiteral("exportGeometry/directory");
  const QString dir =
      settings.value(dirKey, settings.value(QStringLiteral("export/directory"), QDir::homePath())).toString();
  QString path;
  if (core::writesFolder(options.format)) {
    path = QFileDialog::getExistingDirectory(this, tr("Export Geometry: Choose a Folder for the Parts"), dir);
    if (!path.isEmpty()) {
      settings.setValue(dirKey, path);
    }
  } else {
    const QString source = QString::fromStdString(index->document()->sourcePath());
    const QString baseName =
        source.isEmpty() ? QStringLiteral("model") : QFileInfo(source).completeBaseName();
    const QString extension = QString::fromStdString(core::fileExtension(options.format));
    path = QFileDialog::getSaveFileName(this, tr("Export Geometry"),
                                        dir + QLatin1Char('/') + baseName + extension,
                                        geometryFileFilter(options.format));
    if (!path.isEmpty()) {
      if (!path.endsWith(extension, Qt::CaseInsensitive)) {
        path += extension;
      }
      settings.setValue(dirKey, QFileInfo(path).absolutePath());
    }
  }
  if (!path.isEmpty()) {
    exportGeometryTo(path, options, dialog.selectedPartsOnly());
  }
}

void MainWindow::exportGeometryTo(const QString& path, const core::GeometryExportOptions& options,
                                  bool selectedOnly) {
  const std::shared_ptr<const core::AssemblyIndex> index = _scene->model();
  if (!index || _export) {
    return;
  }
  std::vector<core::NodeId> parts;
  if (selectedOnly) {
    parts = _scene->selectedParts();
  } else {
    for (const core::NodeId id : index->partInstances()) {
      if (_scene->isVisible(id)) {
        parts.push_back(id);
      }
    }
  }
  if (parts.empty()) {
    QMessageBox::information(this, tr("Export Geometry"),
                             selectedOnly ? tr("No parts are selected.") : tr("No parts are visible."));
    return;
  }

  // The job keeps the model alive, so closing or replacing it meanwhile is safe; the export
  // simply finishes with the parts it was given.
  _export = new ExportJob(index, std::move(parts), path, options, this);
  const QString name = QFileInfo(path).fileName();
  _exportProgress = new QProgressDialog(tr("Exporting %1...").arg(name), tr("Cancel"), 0, 100, this);
  _exportProgress->setWindowTitle(tr("Export Geometry"));
  _exportProgress->setWindowModality(Qt::WindowModal);
  _exportProgress->setMinimumDuration(400);
  _exportProgress->setAutoClose(false);
  _exportProgress->setAutoReset(false);
  _exportProgress->setValue(0);
  connect(_exportProgress, &QProgressDialog::canceled, _export, &ExportJob::cancel);
  connect(_export, &ExportJob::progress, _exportProgress, [this, name](int percent, const QString& stage) {
    if (_exportProgress) {
      _exportProgress->setLabelText(tr("%1\n%2...").arg(name, stage));
      _exportProgress->setValue(std::min(percent, 99));
    }
  });
  connect(_export, &ExportJob::finished, this, &MainWindow::onExportFinished);
  statusBar()->showMessage(tr("Exporting %1...").arg(name));
  _export->start();
  updateActions();
}

void MainWindow::onExportFinished() {
  ExportJob* job = _export;
  if (job == nullptr) {
    return;
  }
  if (_exportProgress) {
    _exportProgress->close();
    _exportProgress->deleteLater();
  }
  const core::GeometryExportResult& result = job->result();
  const QString name = QFileInfo(job->path()).fileName();
  if (result.cancelled) {
    statusBar()->showMessage(tr("Export of %1 was cancelled.").arg(name), 5000);
  } else if (!result.ok || result.files.empty()) {
    statusBar()->clearMessage();
    QMessageBox::warning(this, tr("Export Geometry"),
                         tr("Could not export %1.\n\n%2").arg(name, QString::fromStdString(result.error)));
  } else {
    QString message;
    if (core::writesFolder(job->options().format)) {
      message = tr("Exported %n part(s)", nullptr, job->partCount()) + QLatin1Char(' ') +
                tr("to %n file(s) in %1", nullptr, static_cast<int>(result.files.size())).arg(job->path());
    } else {
      const qint64 bytes = QFileInfo(QString::fromStdString(result.files.front())).size();
      message =
          tr("Exported %n part(s)", nullptr, job->partCount()) + QLatin1Char(' ') +
          tr("to %1 (%2)").arg(QDir::toNativeSeparators(job->path()), QLocale().formattedDataSize(bytes));
    }
    if (result.skippedFaces > 0) {
      message +=
          QLatin1String("; ") + tr("%n face(s) had no mesh and were left out", nullptr, result.skippedFaces);
    }
    statusBar()->showMessage(message, 10000);
  }
  job->deleteLater();
  _export = nullptr;
  updateActions();
}

// ---------------------------------------------------------------------------------
// Parts list (docs/designs/parts-list-export.md)
// ---------------------------------------------------------------------------------

void MainWindow::exportPartsList() {
  const std::shared_ptr<const core::AssemblyIndex>& index = _scene->model();
  if (!index) {
    return;
  }
  PartsListDialog dialog(this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  QSettings settings;
  const QString dirKey = QStringLiteral("partsList/directory");
  const QString source = QString::fromStdString(index->document()->sourcePath());
  const QString baseName = source.isEmpty() ? QStringLiteral("model") : QFileInfo(source).completeBaseName();
  const QString dir =
      settings.value(dirKey, settings.value(QStringLiteral("export/directory"), QDir::homePath())).toString();
  QString path = QFileDialog::getSaveFileName(
      this, tr("Export Parts List"),
      dir + QLatin1Char('/') + baseName + tr(" parts") + QStringLiteral(".csv"), tr("CSV files (*.csv)"));
  if (path.isEmpty()) {
    return;
  }
  if (!path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) {
    path += QStringLiteral(".csv");
  }
  settings.setValue(dirKey, QFileInfo(path).absolutePath());
  core::PartsListOptions options;
  options.volume = dialog.volume();
  options.mass = dialog.mass();
  options.boundingBox = dialog.boundingBox();
  exportPartsListTo(path, options, dialog.visibleOnly());
}

bool MainWindow::exportPartsListTo(const QString& path, const core::PartsListOptions& requested,
                                   bool visibleOnly) {
  const std::shared_ptr<const core::AssemblyIndex> index = _scene->model();
  if (!index) {
    return false;
  }
  core::PartsListOptions options = requested;
  options.lengthUnit = _measureOptions.lengthUnit;
  options.massUnit = _measureOptions.massUnit;
  options.decimals = _measureOptions.decimals;

  std::vector<core::NodeId> parts;
  for (const core::NodeId part : index->partInstances()) {
    if (!visibleOnly || _scene->isVisible(part)) {
      parts.push_back(part);
    }
  }
  if (parts.empty()) {
    QMessageBox::information(this, tr("Export Parts List"), tr("No parts are visible."));
    return false;
  }

  // Volumes and sizes can take a while on a big model: a progress dialog with Cancel, updated
  // from the computation (on this thread).
  QProgressDialog progressDialog(tr("Measuring parts..."), tr("Cancel"), 0, 100, this);
  progressDialog.setWindowTitle(tr("Export Parts List"));
  progressDialog.setWindowModality(Qt::WindowModal);
  progressDialog.setMinimumDuration(500);
  core::CancelToken cancel;
  const auto onProgress = [&](double fraction, std::string_view) {
    progressDialog.setValue(static_cast<int>(fraction * 100.0));
    QCoreApplication::processEvents();
    if (progressDialog.wasCanceled()) {
      cancel.cancel();
    }
  };
  const core::PartsList list = core::buildPartsList(
      *index, parts, options,
      [this](core::NodeId part) -> std::optional<core::Material> {
        const std::optional<PropertiesPanel::PartMaterial> material = materialOf(part);
        return material ? std::optional<core::Material>(material->material) : std::nullopt;
      },
      onProgress, &cancel);
  progressDialog.close();
  if (list.cancelled || !list.ok) {
    statusBar()->showMessage(tr("Export of the parts list was cancelled."), 5000);
    return false;
  }

  QSaveFile file(path);
  bool written = file.open(QIODevice::WriteOnly);
  if (written) {
    file.write("\xEF\xBB\xBF"); // UTF-8 with BOM, as the Measurements export: spreadsheets show mm³
    file.write(QByteArray::fromStdString(core::toCsv(list, options)));
    written = file.commit();
  }
  if (!written) {
    QMessageBox::warning(this, tr("Export Parts List"),
                         tr("Could not write %1.\n\n%2").arg(path, file.errorString()));
    return false;
  }
  statusBar()->showMessage(tr("Exported %n part(s)", nullptr, static_cast<int>(list.rows.size())) +
                               QLatin1Char(' ') +
                               tr("(%n instance(s))", nullptr, static_cast<int>(parts.size())) +
                               QLatin1Char(' ') + tr("to %1").arg(QDir::toNativeSeparators(path)),
                           8000);
  return true;
}

// ---------------------------------------------------------------------------------
// User saved views (docs/designs/user-saved-views.md)
// ---------------------------------------------------------------------------------

void MainWindow::rebuildSavedViewsMenu() {
  _savedViewsMenu->clear();
  const bool hasModel = _scene->model() != nullptr;
  QAction* save = _savedViewsMenu->addAction(tr("&Save Current View..."), this, &MainWindow::saveCurrentView);
  save->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
  save->setEnabled(hasModel);
  if (!_userViews.empty()) {
    _savedViewsMenu->addSeparator();
  }
  for (int i = 0; i < static_cast<int>(_userViews.size()); ++i) {
    // "&1 Front flange": the first nine get a mnemonic number.
    const QString label =
        i < 9 ? QStringLiteral("&%1 %2").arg(i + 1).arg(_userViews[static_cast<size_t>(i)].name)
              : _userViews[static_cast<size_t>(i)].name;
    _savedViewsMenu->addAction(label, this, [this, i] { restoreUserView(i); });
  }
  _savedViewsMenu->addSeparator();
  QAction* manage =
      _savedViewsMenu->addAction(tr("&Manage Saved Views..."), this, &MainWindow::manageSavedViews);
  manage->setEnabled(hasModel && !_userViews.empty());
}

void MainWindow::loadModelData(const QString& modelPath) {
  _modelPath = modelPath;
  const ModelDataStore::Loaded loaded = _modelStore.load(modelPath);
  _modelData = loaded.data;
  _userViews = userViewsFromJson(_modelData.value(QStringLiteral("views")).toArray());
  _materials.clear();
  const QJsonObject materials = _modelData.value(QStringLiteral("materials")).toObject();
  for (auto it = materials.begin(); it != materials.end(); ++it) {
    const QJsonObject stored = it.value().toObject();
    const double density = stored.value(QStringLiteral("density")).toDouble();
    if (density > 0.0 && std::isfinite(density)) {
      _materials[it.key().toStdString()] =
          core::Material{stored.value(QStringLiteral("name")).toString().toStdString(), density};
    }
  }
  if (!loaded.ok) {
    statusBar()->showMessage(tr("The saved views of this model could not be read (%1).").arg(loaded.error),
                             8000);
  }
  rebuildSavedViewsMenu();
}

void MainWindow::saveModelData() {
  if (_modelPath.isEmpty()) {
    return;
  }
  QString error;
  if (!_modelStore.save(_modelPath, _modelData, &error)) {
    QMessageBox::warning(this, tr("Model Data"),
                         tr("The data kept for this model could not be written.\n\n%1").arg(error));
  }
}

void MainWindow::writeUserViews() {
  _modelData.insert(QStringLiteral("views"), toJson(_userViews));
  saveModelData();
  rebuildSavedViewsMenu();
}

void MainWindow::writeMaterials() {
  QJsonObject materials;
  for (const auto& [prototype, material] : _materials) {
    materials.insert(QString::fromStdString(prototype),
                     QJsonObject{{QStringLiteral("name"), QString::fromStdString(material.name)},
                                 {QStringLiteral("density"), material.density}});
  }
  _modelData.insert(QStringLiteral("materials"), materials);
  saveModelData();
}

std::optional<PropertiesPanel::PartMaterial> MainWindow::materialOf(core::NodeId part) const {
  const std::shared_ptr<const core::AssemblyIndex>& index = _scene->model();
  if (!index) {
    return std::nullopt;
  }
  const auto it = _materials.find(index->prototypeId(part));
  if (it != _materials.end()) {
    return PropertiesPanel::PartMaterial{it->second, false};
  }
  if (std::optional<core::Material> fromFile = index->fileMaterial(part)) {
    return PropertiesPanel::PartMaterial{*fromFile, true};
  }
  return std::nullopt;
}

void MainWindow::setMaterialOfSelection() {
  const std::vector<core::NodeId> nodes = selectedNodes();
  const std::shared_ptr<const core::AssemblyIndex>& index = _scene->model();
  if (!index || nodes.empty()) {
    return;
  }
  // Start from the first part's material, so editing one is quick.
  std::optional<core::Material> current;
  const std::vector<core::NodeId> parts = index->partsBelow(nodes.front());
  if (!parts.empty()) {
    if (const std::optional<PropertiesPanel::PartMaterial> material = materialOf(parts.front())) {
      current = material->material;
    }
  }
  MaterialDialog dialog(current, this);
  if (dialog.exec() == QDialog::Accepted) {
    setMaterial(nodes, dialog.material());
  }
}

void MainWindow::setMaterial(const std::vector<core::NodeId>& nodes,
                             const std::optional<core::Material>& material) {
  const std::shared_ptr<const core::AssemblyIndex>& index = _scene->model();
  if (!index) {
    return;
  }
  std::set<std::string> definitions;
  int instances = 0;
  for (const core::NodeId node : nodes) {
    for (const core::NodeId part : index->partsBelow(node)) {
      definitions.insert(index->prototypeId(part));
    }
  }
  if (definitions.empty()) {
    return;
  }
  for (const std::string& definition : definitions) {
    if (material) {
      _materials[definition] = *material;
    } else {
      _materials.erase(definition);
    }
  }
  for (const core::NodeId part : index->partInstances()) {
    instances += definitions.count(index->prototypeId(part)) != 0 ? 1 : 0;
  }
  writeMaterials();
  _properties->refresh();
  const int count = static_cast<int>(definitions.size());
  statusBar()->showMessage(material ? tr("%1 (%2 kg/m\u00B3) set on %n part(s)", nullptr, count)
                                              .arg(QString::fromStdString(material->name))
                                              .arg(material->density, 0, 'f', 1) +
                                          QLatin1Char(' ') + tr("(%n instance(s)).", nullptr, instances)
                                    : tr("Material cleared on %n part(s).", nullptr, count),
                           6000);
}

void MainWindow::saveCurrentView() {
  if (!_scene->model()) {
    return;
  }
  // The first free "View N".
  QString suggested;
  for (int n = 1;; ++n) {
    suggested = tr("View %1").arg(n);
    const bool taken = std::any_of(_userViews.begin(), _userViews.end(), [&](const UserView& view) {
      return view.name.compare(suggested, Qt::CaseInsensitive) == 0;
    });
    if (!taken) {
      break;
    }
  }
  bool ok = false;
  const QString name =
      QInputDialog::getText(this, tr("Save Current View"), tr("Name:"), QLineEdit::Normal, suggested, &ok)
          .trimmed();
  if (!ok) {
    return;
  }
  if (name.isEmpty()) {
    statusBar()->showMessage(tr("A saved view needs a name."), 5000);
    return;
  }
  const bool exists = std::any_of(_userViews.begin(), _userViews.end(), [&](const UserView& view) {
    return view.name.compare(name, Qt::CaseInsensitive) == 0;
  });
  if (exists &&
      QMessageBox::question(this, tr("Save Current View"),
                            tr("A view named \"%1\" exists. Replace it?").arg(name)) != QMessageBox::Yes) {
    return;
  }
  saveCurrentViewAs(name);
}

bool MainWindow::saveCurrentViewAs(const QString& name) {
  const std::shared_ptr<const core::AssemblyIndex>& index = _scene->model();
  const QString trimmed = name.trimmed();
  if (!index || trimmed.isEmpty()) {
    return false;
  }
  UserView view;
  view.name = trimmed;
  view.camera = _viewport->cameraState();
  const std::vector<std::string> ids = index->pathIds();
  for (const core::NodeId part : index->partInstances()) {
    if (!_scene->isVisible(part)) {
      view.hiddenParts.append(QString::fromStdString(ids[static_cast<size_t>(part)]));
    }
  }
  for (int i = 0; i < render::SectionTool::MaxPlanes; ++i) {
    view.sections[static_cast<size_t>(i)] = _sectionTool->plane(i);
  }
  const auto existing = std::find_if(_userViews.begin(), _userViews.end(), [&](const UserView& other) {
    return other.name.compare(trimmed, Qt::CaseInsensitive) == 0;
  });
  if (existing != _userViews.end()) {
    *existing = std::move(view);
  } else {
    _userViews.push_back(std::move(view));
  }
  writeUserViews();
  statusBar()->showMessage(tr("Saved view \"%1\".").arg(trimmed), 5000);
  return true;
}

void MainWindow::restoreUserView(int index) {
  const std::shared_ptr<const core::AssemblyIndex>& model = _scene->model();
  if (!model || index < 0 || index >= static_cast<int>(_userViews.size())) {
    return;
  }
  const UserView& view = _userViews[static_cast<size_t>(index)];

  // Parts: show all, then hide the stored ones that still exist.
  std::unordered_map<std::string, core::NodeId> byId;
  const std::vector<std::string> ids = model->pathIds();
  for (const core::NodeId part : model->partInstances()) {
    byId.emplace(ids[static_cast<size_t>(part)], part);
  }
  std::vector<core::NodeId> hidden;
  int missing = 0;
  for (const QString& id : view.hiddenParts) {
    const auto it = byId.find(id.toStdString());
    if (it == byId.end()) {
      ++missing;
    } else {
      hidden.push_back(it->second);
    }
  }
  _scene->showAll();
  _scene->setVisible(hidden, false);
  afterVisibilityChange();

  // Section planes, and the section view on or off to match.
  for (int i = 0; i < render::SectionTool::MaxPlanes; ++i) {
    _sectionTool->setPlane(i, view.sections[static_cast<size_t>(i)]);
  }
  _sectionPanel->refresh();
  if (_sectionAction != nullptr) {
    const QSignalBlocker blocker(_sectionAction);
    _sectionAction->setChecked(_sectionTool->isActive());
  }

  // Camera last: AutoZFit sees the restored parts.
  _viewport->setCameraState(view.camera);
  {
    const QSignalBlocker blocker(_perspectiveAction);
    _perspectiveAction->setChecked(view.camera.perspective);
  }
  _viewport->invalidateScene();

  QString message = tr("Restored view \"%1\".").arg(view.name);
  if (missing > 0) {
    message += QLatin1Char(' ') + tr("%n hidden part(s) are no longer in the model.", nullptr, missing);
  }
  statusBar()->showMessage(message, 6000);
}

void MainWindow::manageSavedViews() {
  SavedViewsDialog dialog(_userViews, this);
  if (dialog.exec() == QDialog::Accepted) {
    _userViews = dialog.views();
    writeUserViews();
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
  _viewport->setBackground(preferences.background);
  _viewport->setLighting(preferences.lighting);
  _viewport->setMsaaSamples(preferences.antialiasing ? 4 : 0);
  if (_antialiasingAction != nullptr && _antialiasingAction->isChecked() != preferences.antialiasing) {
    const QSignalBlocker blocker(_antialiasingAction);
    _antialiasingAction->setChecked(preferences.antialiasing);
  }
}

void MainWindow::showGraphicsInfo() {
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Graphics Information"));
  dialog.resize(560, 420);

  auto* text = new QPlainTextEdit(&dialog);
  text->setReadOnly(true);
  text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  const QString info = _viewport->isGlReady() ? _viewport->glInfo()
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
  if (_viewport != nullptr) {
    dialog.setGraphicsInfo(_viewport->glInfo());
  }
  dialog.exec();
}

} // namespace sc::app
