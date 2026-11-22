#pragma once

#include "core/AssemblyIndex.h"
#include "core/PmiModel.h"
#include "core/StepImporter.h"

#include <QWidget>

#include <functional>
#include <optional>

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
  // Where a part's material comes from (docs/designs/material-density.md): set by the user or
  // declared in the file. The window owns the user's materials; the panel asks it.
  struct PartMaterial {
    core::Material material;
    bool fromFile = false;
  };
  using MaterialLookup = std::function<std::optional<PartMaterial>(core::NodeId)>;
  void setMaterialLookup(MaterialLookup lookup) { _materialLookup = std::move(lookup); }
  // Shows the selection again (after its material changed).
  void refresh() { rebuild(); }

  // Shows PMI items instead of the node selection (full semantic content).
  void showPmi(std::shared_ptr<const core::PmiModel> pmi, const std::vector<core::PmiId>& ids);

private:
  QTreeWidgetItem* addGroup(const QString& title);
  void addRow(QTreeWidgetItem* group, const QString& key, const QString& value);
  void rebuild();

  QTreeWidget* _tree = nullptr;
  std::shared_ptr<const core::AssemblyIndex> _index;
  core::ImportStats _stats;
  double _loadSeconds = 0.0;
  std::vector<core::NodeId> _selection;
  std::shared_ptr<const core::PmiModel> _pmi;
  std::vector<core::PmiId> _pmiSelection;
  MaterialLookup _materialLookup;
};

} // namespace sc::app
