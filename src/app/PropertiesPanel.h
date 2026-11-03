#pragma once

#include "core/AssemblyIndex.h"
#include "core/PmiModel.h"
#include "core/StepImporter.h"

#include <QWidget>

#include <memory>
#include <vector>

class QTreeWidget;
class QTreeWidgetItem;

namespace sc::app {

// Read-only property grid: the selected node(s) on top, the model below.
class PropertiesPanel : public QWidget {
  Q_OBJECT

public:
  explicit PropertiesPanel(QWidget* parent = nullptr);

  void setModel(std::shared_ptr<const core::AssemblyIndex> index, const core::ImportStats& stats,
                double loadSeconds);
  void clear();
  void showSelection(const std::vector<core::NodeId>& nodes);
  // Shows PMI items instead of the node selection (full semantic content).
  void showPmi(std::shared_ptr<const core::PmiModel> pmi, const std::vector<core::PmiId>& ids);

private:
  QTreeWidgetItem* addGroup(const QString& title);
  void addRow(QTreeWidgetItem* group, const QString& key, const QString& value);
  void rebuild();

  QTreeWidget* m_tree = nullptr;
  std::shared_ptr<const core::AssemblyIndex> m_index;
  core::ImportStats m_stats;
  double m_loadSeconds = 0.0;
  std::vector<core::NodeId> m_selection;
  std::shared_ptr<const core::PmiModel> m_pmi;
  std::vector<core::PmiId> m_pmiSelection;
};

} // namespace sc::app
