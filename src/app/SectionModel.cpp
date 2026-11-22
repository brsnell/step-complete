#include "app/SectionModel.h"

namespace sc::app {

SectionToolModel::SectionToolModel(render::SectionTool* tool, QObject* parent)
    : SectionModel(parent), _tool(tool) {
  connect(tool, &render::SectionTool::planeChanged, this, &SectionModel::planeChanged);
  connect(tool, &render::SectionTool::pickStarted, this, &SectionModel::pickChanged);
  connect(tool, &render::SectionTool::pickEnded, this, &SectionModel::pickChanged);
}

} // namespace sc::app
