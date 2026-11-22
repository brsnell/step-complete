#pragma once

#include "core/AssemblyIndex.h"
#include "core/GeometryExport.h"
#include "core/Measurement.h"
#include "core/PmiModel.h"

#include <QMainWindow>
#include <QPointer>
#include <QString>

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
class OcctViewport;
class SceneManager;
} // namespace sc::render

namespace sc::app {

class AssemblyFilterProxy;
class AssemblyTreeModel;
class ImportJob;
class MeasurementsPanel;
class ExportJob;
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

  void setSectionView(bool on);

  // --- PMI --------------------------------------------------------------------
  void onPmiSelected(const std::vector<core::PmiId>& ids);
  void activateSavedView(int viewId);
  void showAllPmi();

  void showGraphicsInfo();

  // --- Export and view preferences ---------------------------------------------
  void exportImage();
  void exportGeometry();
  void onExportFinished();
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
  MeasurementsPanel* _measurements = nullptr;
  QDockWidget* _measurementsDock = nullptr;
  QActionGroup* _measureGroup = nullptr;
  QAction* _stopMeasureAction = nullptr;
  QAction* _massPropertiesAction = nullptr;
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
  bool _glReady = false;
  bool _syncingSelection = false;
};

} // namespace sc::app
