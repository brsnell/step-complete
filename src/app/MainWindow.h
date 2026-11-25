#pragma once

#include "app/PropertiesPanel.h"
#include "app/UserViews.h"
#include "core/AssemblyIndex.h"
#include "core/Clash.h"
#include "core/GeometryExport.h"
#include "core/Markup.h"
#include "core/Measurement.h"
#include "core/PartsList.h"
#include "core/PmiModel.h"

#include <QMainWindow>
#include <QPointer>
#include <QString>

#include <map>
#include <memory>
#include <vector>

class QAction;
class QActionGroup;
class QDockWidget;
class QLabel;
class QLineEdit;
class QModelIndex;
class QListWidget;
class QMenu;
class QProgressDialog;
class QTimer;
class QTreeView;

namespace sc::render {
class MeasureTool;
class SectionTool;
class PmiScene;
class ThicknessScene;
class ComparisonScene;
class MarkupScene;
class OcctViewport;
class SceneManager;
} // namespace sc::render

namespace sc::app {

class AssemblyFilterProxy;
class AssemblyTreeModel;
class ImportJob;
class MarkupPanel;
class MeasurementsPanel;
class ExportJob;
class ClashJob;
class ComparisonJob;
class ComparisonPanel;
class ClashPanel;
class GeometryCheckJob;
class GeometryCheckPanel;
class ThicknessJob;
class ThicknessPanel;
class PmiPanel;
class SectionPanel;
struct ViewPreferences;
class PropertiesPanel;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  // Opens the file as soon as the 3D view is ready (used for command-line arguments).
  void openFileWhenReady(const QString& path);

  // Exports the visible (or selected) parts in the background, with a progress dialog: what
  // File > Export Geometry does once its dialogs are answered. 'path' is the file, or the folder
  // for one STL per part. Does nothing while another export runs.
  void exportGeometryTo(const QString& path, const core::GeometryExportOptions& options, bool selectedOnly);

  // What File > Export Parts List does once its dialogs are answered: write the CSV bill of
  // materials of all (or the visible) parts to 'path'. Returns false on cancel or failure (a
  // failure is shown in a message box).
  bool exportPartsListTo(const QString& path, const core::PartsListOptions& options, bool visibleOnly);

  // --- User saved views (docs/designs/user-saved-views.md) ---------------------------
  // What View > Saved Views does: save the current camera, hidden parts and section planes
  // under a name (replacing a view of that name), and restore a view by its position.
  bool saveCurrentViewAs(const QString& name);
  void restoreUserView(int index);
  const std::vector<UserView>& userViews() const { return _userViews; }

  // --- Materials (docs/designs/material-density.md) ------------------------------------
  // What Measure > Set Material and Clear Material do: give the part definitions below 'nodes'
  // a material (nullopt clears it, back to the file's). Stored per model file.
  void setMaterial(const std::vector<core::NodeId>& nodes, const std::optional<core::Material>& material);
  // A part's material: the user's, else the one the file declares.
  std::optional<PropertiesPanel::PartMaterial> materialOf(core::NodeId part) const;

  // --- Geometry check (docs/designs/geometry-check.md) --------------------------------
  // What Measure > Check Geometry does: check the selected parts (every visible part when
  // nothing is selected) in the background and list the problems in the Geometry Check panel.
  // Does nothing while a check runs.
  void checkGeometry();
  bool isCheckingGeometry() const;

  // --- Clash detection (docs/designs/clash-detection.md) -------------------------------
  // What Measure > Check Clashes does once its dialog is answered: check the selected parts (every
  // visible part when nothing is selected) against each other in the background and list the
  // pairs in the Clashes panel. Does nothing while a clash check runs.
  void checkClashesWith(const core::ClashOptions& options);
  bool isCheckingClashes() const;

  // --- Wall thickness (docs/designs/wall-thickness.md) -----------------------------------
  // What Measure > Wall Thickness does: on, measure the selected parts (every visible part when
  // nothing is selected) in the background and color them by thickness; off, back to normal.
  void showWallThickness(bool on);
  bool isMeasuringThickness() const;
  bool isThicknessShown() const;

  // --- Model comparison (docs/designs/model-comparison.md) ----------------------------
  // What File > Compare With does once a file is chosen: import it as the other revision and
  // compare the open model with it in the background, then color the changes.
  void compareWith(const QString& path);
  bool isComparing() const;       // the comparison job runs
  bool isComparisonShown() const; // the comparison view is on

  // --- Markup (docs/designs/markup.md) -------------------------------------------------
  // The open model's notes and strokes, kept in the per-model store. Each change is written at
  // once and shown in the view and the Markup panel.
  const core::Markup& markup() const { return _markup; }
  render::MarkupScene* markupScene() const { return _markupScene; }
  // Returns the new item's id, or 0 (no model, or a note without text).
  int addMarkupNote(const core::MarkupNote& note);
  int addMarkupStroke(const core::MarkupStroke& stroke);
  bool setMarkupNoteText(int id, const QString& text); // empty text is refused
  void deleteMarkup(const std::vector<int>& ids);
  void deleteAllMarkup(); // what Delete All Markup does once confirmed
  // What Export Markup and Import Markup do once a file is chosen. Export shows a message box
  // on failure; import adds the file's items to the model's and returns how many, or -1 with
  // the reason in 'error'.
  bool exportMarkupTo(const QString& path);
  int importMarkupFrom(const QString& path, QString* error = nullptr);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  void createActions();
  void createDocks();
  void onGlInitialized(bool ok, const QString& message);

  // --- Files ------------------------------------------------------------------
  void openFileDialog();
  void openFile(const QString& path);
  void onImportFinished();
  void closeModel();
  void addRecentFile(const QString& path);
  void rebuildRecentMenu();

  // --- Selection --------------------------------------------------------------
  std::vector<core::NodeId> selectedNodes() const;
  void onTreeSelectionChanged();
  void onViewportSelectionChanged();
  // Filters the Assembly tree by the filter box's text and expands to the matches.
  void applyTreeFilter();
  // Expands the rows that have a match below them; returns whether the subtree has one.
  bool expandToMatches(const QModelIndex& parent);
  void focusTreeFilter();

  // --- View commands ------------------------------------------------------------
  void hideSelected();
  void isolateSelected();
  void showAll();
  void toggleTransparencySelected();
  void fitSelection();
  void setNodeVisible(core::NodeId node, bool visible);
  void afterVisibilityChange();
  void showContextMenu(QWidget* source, const QPoint& pos);
  void updateActions();
  // The open model came from a mesh file (STL, OBJ, glTF): no exact geometry to measure.
  bool isMeshModel() const;
  QString meshNotice() const;

  // --- Measurement ------------------------------------------------------------
  void createMeasureMenu();
  void setMeasureMode(int mode); // -1 = off, else core::MeasureMode
  void measureMassProperties();
  void onMeasured(const core::Measurement& measurement);
  void syncMeasurementWitnesses();
  void onDevicePixelRatioChanged();
  void declutterPmi();
  void onPickProgress(int picked, int required);
  void applyMeasureOptions();

  // The visible parts below the selection, or every visible part when the selection is empty.
  std::vector<core::NodeId> visiblePartsOf(const std::vector<core::NodeId>& selection) const;
  void onGeometryCheckFinished();
  void showGeometryIssue(int index, bool zoom);
  void stopGeometryCheck(); // cancels a running check and clears the panel and highlight
  void checkClashes();      // asks for the options, then checkClashesWith
  void startClashJob(std::vector<core::NodeId> parts, const core::ClashOptions& options);
  void onClashCheckFinished();
  void showClash(int index, bool zoom);
  void stopClashCheck();
  void onThicknessFinished();
  void onThicknessHover();
  void stopThickness(); // cancels, clears the view and the panel, unchecks the menu item
  void compareWithDialog();
  void startComparison(const QString& path, std::shared_ptr<const core::AssemblyIndex> other,
                       double tolerance);
  void onComparisonFinished();
  void showChange(int changeIndex, bool zoom);
  void stopComparison(); // cancels, clears the view and the panel
  // The red highlight is shared by the Geometry Check and Clashes panels; each clears only its own.
  enum class Highlight { None, GeometryIssue, Clash };
  void showHighlight(Highlight owner, const TopoDS_Shape& worldShape);
  void clearHighlight(Highlight owner);

  void setSectionView(bool on);

  // --- Markup -----------------------------------------------------------------
  void createMarkupMenu();
  void writeMarkup();   // to the store, then refreshMarkup
  void refreshMarkup(); // the view and the panel
  void startAddNote();
  void onNotePicked(const core::MarkupNote& note);
  void editMarkupNote(int id);
  void setSketching(bool on);
  void zoomToMarkup(int id);
  void exportMarkup();
  void importMarkup();
  void confirmDeleteAllMarkup();
  // Adds markup read from a file; returns how many items were added.
  int appendMarkup(const core::Markup& markup);

  // --- PMI --------------------------------------------------------------------
  void onPmiSelected(const std::vector<core::PmiId>& ids);
  void activateSavedView(int viewId);
  void showAllPmi();

  void showGraphicsInfo();
  void openCrashReports(); // Help > Crash Reports
  // Keeps the crash report's "Activity" line current (docs/designs/crash-reporting.md).
  void updateCrashActivity();

  // --- Export and view preferences ---------------------------------------------
  void exportImage();
  void exportGeometry();
  void onExportFinished();
  void exportPartsList();

  void saveCurrentView();
  void manageSavedViews();
  void rebuildSavedViewsMenu();
  void loadModelData(const QString& modelPath);
  void saveModelData();
  void writeUserViews();
  void writeMaterials();
  void setMaterialOfSelection();
  void showPreferences();
  void applyViewPreferences(const ViewPreferences& preferences);
  void showAbout();

  render::OcctViewport* _viewport = nullptr;
  std::unique_ptr<render::SceneManager> _scene;
  AssemblyTreeModel* _treeModel = nullptr;
  AssemblyFilterProxy* _treeProxy = nullptr;
  QLineEdit* _treeFilter = nullptr;
  QTreeView* _treeView = nullptr;
  QDockWidget* _treeDock = nullptr;
  PropertiesPanel* _properties = nullptr;
  QListWidget* _messages = nullptr;
  render::MeasureTool* _measureTool = nullptr;
  std::unique_ptr<render::PmiScene> _pmiScene;
  std::shared_ptr<const core::PmiModel> _pmi;
  PmiPanel* _pmiPanel = nullptr;
  QDockWidget* _pmiDock = nullptr;
  QAction* _showAllPmiAction = nullptr;
  QAction* _semanticLabelsAction = nullptr;
  render::SectionTool* _sectionTool = nullptr;
  SectionPanel* _sectionPanel = nullptr;
  QDockWidget* _sectionDock = nullptr;
  QAction* _sectionAction = nullptr;
  QAction* _cancelPickAction = nullptr; // Esc while a section plane waits for a face
  QTimer* _declutterTimer = nullptr;    // re-lays out PMI labels once the camera settles
  QAction* _antialiasingAction = nullptr;
  QAction* _exportImageAction = nullptr;
  QAction* _exportGeometryAction = nullptr;
  QAction* _exportPartsListAction = nullptr;
  QAction* _perspectiveAction = nullptr;
  QMenu* _savedViewsMenu = nullptr;
  ModelDataStore _modelStore{ModelDataStore::defaultDirectory()};
  QString _modelPath;     // the open model's file, for its saved views
  QJsonObject _modelData; // everything the store keeps for it (views and later features)
  std::vector<UserView> _userViews;
  std::map<std::string, core::Material> _materials; // by AssemblyIndex::prototypeId
  QAction* _setMaterialAction = nullptr;
  QAction* _clearMaterialAction = nullptr;
  MeasurementsPanel* _measurements = nullptr;
  QDockWidget* _measurementsDock = nullptr;
  QActionGroup* _measureGroup = nullptr;
  QAction* _stopMeasureAction = nullptr;
  QAction* _massPropertiesAction = nullptr;
  QAction* _checkGeometryAction = nullptr;
  GeometryCheckPanel* _geometryCheck = nullptr;
  QDockWidget* _geometryCheckDock = nullptr;
  QPointer<GeometryCheckJob> _geometryCheckJob;
  std::shared_ptr<const core::AssemblyIndex> _checkedIndex; // the model the listed problems are in
  QAction* _checkClashesAction = nullptr;
  ClashPanel* _clashPanel = nullptr;
  QDockWidget* _clashDock = nullptr;
  QPointer<ClashJob> _clashJob;
  std::shared_ptr<const core::AssemblyIndex> _clashIndex; // the model the listed clashes are in
  std::vector<core::NodeId> _clashParts;                  // what the last check covered, for Check Again
  core::ClashOptions _clashOptions;
  Highlight _highlight = Highlight::None;
  std::unique_ptr<render::ThicknessScene> _thicknessScene;
  QAction* _thicknessAction = nullptr;
  ThicknessPanel* _thicknessPanel = nullptr;
  QDockWidget* _thicknessDock = nullptr;
  QPointer<ThicknessJob> _thicknessJob;
  bool _thicknessMarker = false; // the overlay marker shows the thickness readout
  std::unique_ptr<render::ComparisonScene> _comparisonScene;
  QAction* _compareAction = nullptr;
  ComparisonPanel* _comparisonPanel = nullptr;
  QDockWidget* _comparisonDock = nullptr;
  QPointer<ComparisonJob> _comparisonJob;
  QString _comparisonPath; // the other revision's file
  render::MarkupScene* _markupScene = nullptr;
  core::Markup _markup;
  MarkupPanel* _markupPanel = nullptr;
  QDockWidget* _markupDock = nullptr;
  QAction* _addNoteAction = nullptr;
  QAction* _sketchAction = nullptr;
  QAction* _showMarkupAction = nullptr;
  QAction* _exportMarkupAction = nullptr;
  QAction* _importMarkupAction = nullptr;
  QAction* _deleteAllMarkupAction = nullptr;
  core::MeasureDisplayOptions _measureOptions;
  QDockWidget* _messagesDock = nullptr;
  QLabel* _emptyHint = nullptr;
  QLabel* _glStatus = nullptr;
  QLabel* _unitStatus = nullptr;
  QMenu* _recentMenu = nullptr;
  QActionGroup* _styleGroup = nullptr;

  QAction* _closeAction = nullptr;
  QAction* _hideAction = nullptr;
  QAction* _isolateAction = nullptr;
  QAction* _showAllAction = nullptr;
  QAction* _transparentAction = nullptr;
  QAction* _fitSelectionAction = nullptr;

  QPointer<ImportJob> _import;
  QPointer<QProgressDialog> _progress;
  QPointer<ExportJob> _export;
  QPointer<QProgressDialog> _exportProgress;
  QString _pendingFile;
  std::vector<core::NodeId> _transparentParts;
  QString _crashActivity;
  bool _glReady = false;
  bool _syncingSelection = false;
};

} // namespace sc::app
