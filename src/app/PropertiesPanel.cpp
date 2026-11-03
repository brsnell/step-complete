#include "app/PropertiesPanel.h"

#include "core/Units.h"

#include <QFileInfo>
#include <QHeaderView>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace sc::app {

namespace {

QString number(double value, int decimals = 3) {
  return QString::fromStdString(core::formatNumber(value, decimals));
}

QString colorText(const Quantity_ColorRGBA& color) {
  const Quantity_Color& rgb = color.GetRGB();
  // Quantity_Color stores linear RGB; show the familiar sRGB 0-255 values.
  NCollection_Vec3<float> srgb = Quantity_Color::Convert_LinearRGB_To_sRGB(
      NCollection_Vec3<float>(float(rgb.Red()), float(rgb.Green()), float(rgb.Blue())));
  return QStringLiteral("RGB(%1, %2, %3)")
      .arg(qRound(srgb.r() * 255.0f))
      .arg(qRound(srgb.g() * 255.0f))
      .arg(qRound(srgb.b() * 255.0f));
}

} // namespace

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent) {
  m_tree = new QTreeWidget(this);
  m_tree->setColumnCount(2);
  m_tree->setHeaderLabels({tr("Property"), tr("Value")});
  m_tree->setRootIsDecorated(false);
  m_tree->setIndentation(8);
  m_tree->setAlternatingRowColors(true);
  m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_tree->header()->setStretchLastSection(true);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_tree);
}

void PropertiesPanel::setModel(std::shared_ptr<const core::AssemblyIndex> index,
                               const core::ImportStats& stats, double loadSeconds) {
  m_index = std::move(index);
  m_stats = stats;
  m_loadSeconds = loadSeconds;
  m_selection.clear();
  m_pmi.reset();
  m_pmiSelection.clear();
  rebuild();
}

void PropertiesPanel::clear() {
  m_index.reset();
  m_selection.clear();
  m_pmi.reset();
  m_pmiSelection.clear();
  rebuild();
}

void PropertiesPanel::showSelection(const std::vector<core::NodeId>& nodes) {
  m_selection = nodes;
  m_pmiSelection.clear();
  rebuild();
}

void PropertiesPanel::showPmi(std::shared_ptr<const core::PmiModel> pmi,
                              const std::vector<core::PmiId>& ids) {
  m_pmi = std::move(pmi);
  m_pmiSelection = ids;
  m_selection.clear();
  rebuild();
}

QTreeWidgetItem* PropertiesPanel::addGroup(const QString& title) {
  auto* group = new QTreeWidgetItem(m_tree, {title});
  QFont font = group->font(0);
  font.setBold(true);
  group->setFont(0, font);
  group->setFirstColumnSpanned(true);
  group->setExpanded(true);
  return group;
}

void PropertiesPanel::addRow(QTreeWidgetItem* group, const QString& key, const QString& value) {
  auto* item = new QTreeWidgetItem(group, {key, value});
  item->setToolTip(1, value);
}

void PropertiesPanel::rebuild() {
  m_tree->clear();
  if (!m_index) {
    return;
  }

  // --- PMI ------------------------------------------------------------------------
  if (m_pmi && m_pmiSelection.size() == 1) {
    const core::PmiItem& item = m_pmi->item(m_pmiSelection.front());
    QTreeWidgetItem* group = addGroup(QString::fromUtf8(core::toString(item.kind)));
    const auto hasProperty = [&item](const char* key) {
      return std::any_of(item.properties.begin(), item.properties.end(),
                         [key](const core::pmi::Property& p) { return p.first == key; });
    };
    addRow(group, tr("Name"), QString::fromStdString(item.name));
    if (!hasProperty("Type")) {
      addRow(group, tr("Type"), QString::fromStdString(item.typeName));
    }
    if (!item.text.empty() && !hasProperty("Text")) {
      addRow(group, tr("Text"), QString::fromStdString(item.text));
    }
    if (!item.semantic) {
      addRow(group, tr("Semantics"), tr("Not interpreted (graphical presentation only)"));
    }
    for (const auto& [key, value] : item.properties) {
      addRow(group, QString::fromStdString(key), QString::fromStdString(value));
    }
    if (!item.datums.empty()) {
      QStringList datums;
      for (const core::PmiId datum : item.datums) {
        datums << QString::fromStdString(m_pmi->item(datum).text);
      }
      addRow(group, tr("Datum references"), datums.join(QStringLiteral(" | ")));
    }
    addRow(group, tr("Applies to"),
           tr("%n entities", nullptr, static_cast<int>(item.targets.size() + item.secondTargets.size())));
    addRow(group, tr("Presentation"),
           item.presentation.IsNull() ? tr("Semantic only") : tr("Graphical (from file)"));
  } else if (m_pmi && m_pmiSelection.size() > 1) {
    QTreeWidgetItem* group = addGroup(tr("PMI"));
    addRow(group, tr("Selected annotations"), QString::number(m_pmiSelection.size()));
  }

  // --- Selection ------------------------------------------------------------------
  if (m_selection.size() == 1) {
    const core::NodeId id = m_selection.front();
    const core::AssemblyNode& node = m_index->node(id);
    QTreeWidgetItem* group = addGroup(node.kind == core::NodeKind::Assembly ? tr("Assembly") : tr("Part"));
    addRow(group, tr("Name"), QString::fromStdString(node.name));
    if (node.prototypeName != node.name && !node.prototypeName.empty()) {
      addRow(group, tr("Product"), QString::fromStdString(node.prototypeName));
    }
    addRow(group, tr("Path"), QString::fromStdString(m_index->displayPath(id)));
    addRow(group, tr("Instances in model"), QString::number(m_index->instanceCount(id)));
    if (node.kind == core::NodeKind::Assembly) {
      addRow(group, tr("Children"), QString::number(node.children.size()));
      addRow(group, tr("Parts below"), QString::number(m_index->partsBelow(id).size()));
    }
    const gp_XYZ position = node.location.Transformation().TranslationPart();
    addRow(
        group, tr("Position (mm)"),
        QStringLiteral("%1, %2, %3").arg(number(position.X()), number(position.Y()), number(position.Z())));
    if (node.inheritedColor) {
      addRow(group, tr("Instance color"), colorText(*node.inheritedColor));
    }
    const auto layers = m_index->layers(id);
    if (!layers.empty()) {
      QStringList names;
      for (const auto& layer : layers) {
        names << QString::fromStdString(layer);
      }
      addRow(group, tr("Layers"), names.join(QStringLiteral(", ")));
    }
    if (!node.visibleInFile) {
      addRow(group, tr("Hidden in file"), tr("Yes"));
    }
    const auto attributes = m_index->attributes(id);
    if (!attributes.empty()) {
      QTreeWidgetItem* attributeGroup = addGroup(tr("Attributes"));
      for (const auto& [key, value] : attributes) {
        addRow(attributeGroup, QString::fromStdString(key), QString::fromStdString(value));
      }
    }
  } else if (m_selection.size() > 1) {
    QTreeWidgetItem* group = addGroup(tr("Selection"));
    addRow(group, tr("Selected items"), QString::number(m_selection.size()));
    size_t parts = 0;
    for (const core::NodeId id : m_selection) {
      parts += m_index->partsBelow(id).size();
    }
    addRow(group, tr("Part instances"), QString::number(parts));
  }

  // --- Model ----------------------------------------------------------------------
  QTreeWidgetItem* model = addGroup(tr("Model"));
  const auto& document = m_index->document();
  if (document) {
    const QFileInfo file(QString::fromStdString(document->sourcePath()));
    addRow(model, tr("File"), file.fileName());
    addRow(model, tr("Folder"), file.absolutePath());
    if (file.exists()) {
      addRow(model, tr("Size"), QLocale().formattedDataSize(file.size()));
    }
    QString unit = QString::fromStdString(document->fileUnitName());
    if (document->fileUnit()) {
      unit = QString::fromUtf8(core::displayName(*document->fileUnit()).data());
    }
    addRow(model, tr("File unit"), unit.isEmpty() ? tr("Unknown") : unit);
  }
  addRow(model, tr("Part instances"), QString::number(m_index->partInstances().size()));
  addRow(model, tr("Unique parts"), QString::number(m_index->uniquePartCount()));
  addRow(model, tr("Load time"),
         tr("%1 s (read %2 s, build %3 s, mesh %4 s)")
             .arg(number(m_loadSeconds, 2), number(m_stats.readSeconds, 2),
                  number(m_stats.transferSeconds, 2), number(m_stats.meshSeconds, 2)));
}

} // namespace sc::app
