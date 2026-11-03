#include "app/SectionModel.h"

namespace sc::app {

SectionToolModel::SectionToolModel(render::SectionTool* tool, QObject* parent)
    : SectionModel(parent), m_tool(tool) {
  connect(tool, &render::SectionTool::planeChanged, this, &SectionModel::planeChanged);
}

} // namespace sc::app
