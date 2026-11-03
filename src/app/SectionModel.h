#pragma once

#include "render/SectionTool.h"

#include <QObject>

#include <utility>

namespace sc::app {

// What SectionPanel needs from the section engine. An interface so the panel can
// be tested without an OpenGL viewport; SectionToolModel adapts the real tool.
class SectionModel : public QObject {
  Q_OBJECT

public:
  using QObject::QObject;

  virtual const render::SectionPlane& plane(int index) const = 0;
  virtual void setPlane(int index, const render::SectionPlane& plane) = 0;
  // Offsets that cross the model along the plane's normal (min, max).
  virtual std::pair<double, double> offsetRange(int index) const = 0;
  // Reverses the normal keeping the plane where it is.
  virtual void flip(int index) = 0;
  // Disables all planes.
  virtual void reset() = 0;
  virtual bool handlesVisible() const = 0;
  virtual void setHandlesVisible(bool visible) = 0;

Q_SIGNALS:
  // A plane changed, for whatever reason (including drags in the 3D view).
  void planeChanged(int index);
};

// Adapter over a (non-owned) render::SectionTool.
class SectionToolModel : public SectionModel {
  Q_OBJECT

public:
  explicit SectionToolModel(render::SectionTool* tool, QObject* parent = nullptr);

  const render::SectionPlane& plane(int index) const override { return m_tool->plane(index); }
  void setPlane(int index, const render::SectionPlane& plane) override { m_tool->setPlane(index, plane); }
  std::pair<double, double> offsetRange(int index) const override { return m_tool->offsetRange(index); }
  void flip(int index) override { m_tool->flip(index); }
  void reset() override { m_tool->reset(); }
  bool handlesVisible() const override { return m_tool->handlesVisible(); }
  void setHandlesVisible(bool visible) override { m_tool->setHandlesVisible(visible); }

private:
  render::SectionTool* m_tool;
};

} // namespace sc::app
