#include "app/SectionModel.h"

namespace sc::app {

SectionToolModel::SectionToolModel(render::SectionTool* tool, QObject* parent)
    : SectionModel(parent), _tool(tool) {
  connect(tool, &render::SectionTool::planeChanged, this, &SectionModel::planeChanged);
}

} // namespace sc::app
