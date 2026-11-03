#include "app/MainWindow.h"

#include "app/AssemblyTreeModel.h"
#include "app/ImportJob.h"
#include "app/MeasurementsPanel.h"
#include "app/PropertiesPanel.h"
#include "core/OcctInfo.h"
#include "core/Version.h"
#include "render/MeasureTool.h"
#include "render/OcctViewport.h"
#include "render/SceneManager.h"
#include "render/SceneStyle.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
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

  m_measurements = new MeasurementsPanel(this);
  m_measurementsDock = new QDockWidget(tr("Measurements"), this);
  m_measurementsDock->setObjectName(QStringLiteral("MeasurementsDock"));
  m_measurementsDock->setWidget(m_measurements);
  addDockWidget(Qt::RightDockWidgetArea, m_measurementsDock);
  resizeDocks({m_measurementsDock}, {320}, Qt::Horizontal);
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

  viewMenu->addSeparator();
  QAction* antialiasing = viewMenu->addAction(tr("&Antialiasing (4x MSAA)"));
  antialiasing->setCheckable(true);
  connect(antialiasing, &QAction::toggled, this, [this](bool on) { m_viewport->setMsaaSamples(on ? 4 : 0); });
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

void MainWindow::updateActions() {
  const bool hasModel = m_scene->model() != nullptr;
  const bool hasSelection = hasModel && !selectedNodes().empty();
  m_closeAction->setEnabled(hasModel);
  m_hideAction->setEnabled(hasSelection);
  m_isolateAction->setEnabled(hasSelection);
  m_transparentAction->setEnabled(hasSelection);
  m_fitSelectionAction->setEnabled(hasSelection);
  m_showAllAction->setEnabled(hasModel);
  if (m_stopMeasureAction != nullptr) {
    m_stopMeasureAction->setEnabled(m_measureTool->isActive());
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
    m_scene->setModel(index);
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
  m_properties->showSelection(selectedNodes());
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
  const QString text = tr("<h3>%1 %2</h3>"
                          "<p>%3</p>"
                          "<p>Licensed under the MIT License.</p>"
                          "<p>Built with:<br>"
                          "Open CASCADE Technology %4 (LGPL 2.1 with exception)<br>"
                          "Qt %5 (LGPL 3)</p>"
                          "<p>Qt and Open CASCADE are dynamically linked and can be replaced. "
                          "See THIRD_PARTY_NOTICES.md for full license texts and sources.</p>")
                           .arg(QString::fromUtf8(core::kAppName), QString::fromUtf8(core::kAppVersion),
                                QString::fromUtf8(core::kAppDescription),
                                QString::fromStdString(core::occtVersion()), QString::fromLatin1(qVersion()));
  QMessageBox::about(this, tr("About %1").arg(QString::fromUtf8(core::kAppName)), text);
}

} // namespace sc::app
