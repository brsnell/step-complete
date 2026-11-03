#pragma once

#include "core/AssemblyIndex.h"
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
class QListWidget;
class QMenu;
class QProgressDialog;
class QTreeView;

namespace sc::render {
class MeasureTool;
class PmiScene;
class OcctViewport;
class SceneManager;
} // namespace sc::render

namespace sc::app {

class AssemblyTreeModel;
class ImportJob;
class MeasurementsPanel;
class PmiPanel;
struct ViewPreferences;
class PropertiesPanel;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  // Opens the file as soon as the 3D view is ready (used for command-line arguments).
  void openFileWhenReady(const QString& path);

protected:
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
  void onPickProgress(int picked, int required);
  void applyMeasureOptions();

  // --- PMI --------------------------------------------------------------------
  void onPmiSelected(const std::vector<core::PmiId>& ids);
  void activateSavedView(int viewId);
  void showAllPmi();

  void showGraphicsInfo();

  // --- Export and view preferences ---------------------------------------------
  void exportImage();
  void showPreferences();
  void applyViewPreferences(const ViewPreferences& preferences);
  void showAbout();

  render::OcctViewport* m_viewport = nullptr;
  std::unique_ptr<render::SceneManager> m_scene;
  AssemblyTreeModel* m_treeModel = nullptr;
  QTreeView* m_treeView = nullptr;
  PropertiesPanel* m_properties = nullptr;
  QListWidget* m_messages = nullptr;
  render::MeasureTool* m_measureTool = nullptr;
  std::unique_ptr<render::PmiScene> m_pmiScene;
  std::shared_ptr<const core::PmiModel> m_pmi;
  PmiPanel* m_pmiPanel = nullptr;
  QDockWidget* m_pmiDock = nullptr;
  QAction* m_showAllPmiAction = nullptr;
  QAction* m_antialiasingAction = nullptr;
  QAction* m_exportImageAction = nullptr;
  MeasurementsPanel* m_measurements = nullptr;
  QDockWidget* m_measurementsDock = nullptr;
  QActionGroup* m_measureGroup = nullptr;
  QAction* m_stopMeasureAction = nullptr;
  QAction* m_massPropertiesAction = nullptr;
  core::MeasureDisplayOptions m_measureOptions;
  QDockWidget* m_messagesDock = nullptr;
  QLabel* m_emptyHint = nullptr;
  QLabel* m_glStatus = nullptr;
  QLabel* m_unitStatus = nullptr;
  QMenu* m_recentMenu = nullptr;
  QActionGroup* m_styleGroup = nullptr;

  QAction* m_closeAction = nullptr;
  QAction* m_hideAction = nullptr;
  QAction* m_isolateAction = nullptr;
  QAction* m_showAllAction = nullptr;
  QAction* m_transparentAction = nullptr;
  QAction* m_fitSelectionAction = nullptr;

  QPointer<ImportJob> m_import;
  QPointer<QProgressDialog> m_progress;
  QString m_pendingFile;
  std::vector<core::NodeId> m_transparentParts;
  bool m_glReady = false;
  bool m_syncingSelection = false;
};

} // namespace sc::app
