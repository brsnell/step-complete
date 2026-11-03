#pragma once

#include <QWidget>

#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QPushButton;
class QSlider;

namespace sc::render {
class SectionTool;
}

namespace sc::app {

class SectionModel;

// Controls for the section view: three plane rows (enable, axis, offset, flip,
// cap, hatch), "Show handles" and "Reset". Talks to a render::SectionTool and
// follows it when the tool changes (e.g. dragging a plane handle in 3D).
class SectionPanel : public QWidget {
  Q_OBJECT

public:
  static constexpr int kSliderSteps = 1000;

  // The model is not owned and must outlive the panel.
  explicit SectionPanel(SectionModel* model, QWidget* parent = nullptr);
  // Convenience: wraps the tool in a SectionToolModel owned by the panel.
  explicit SectionPanel(render::SectionTool* tool, QWidget* parent = nullptr);

  // Re-reads every plane from the tool (call after loading a model: offset ranges change).
  void refresh();

Q_SIGNALS:
  // The user changed something; the view needs a redraw. Not emitted for tool-driven updates.
  void sectionChanged();

private:
  struct Row {
    QCheckBox* enable = nullptr;
    QComboBox* axis = nullptr;
    QSlider* slider = nullptr;
    QDoubleSpinBox* offset = nullptr;
    QPushButton* flip = nullptr;
    QCheckBox* cap = nullptr;
    QCheckBox* hatch = nullptr;
    QWidget* controls = nullptr; // everything except the enable box
  };

  void buildRow(int index, QGridLayout* layout);
  void refreshRow(int index);
  void onToolChanged(int index);
  void setOffset(int index, double offset);
  void onAxisChosen(int index);
  void onFlip(int index);
  void onReset();
  // Writes a modified copy of the plane to the tool, updates the row and announces the change.
  template <typename Fn>
  void edit(int index, Fn&& change);

  SectionModel* m_model;
  std::array<Row, 3> m_rows;
  QCheckBox* m_handles = nullptr;
  bool m_busy = false; // our own writes / widget updates: ignore echoes
};

} // namespace sc::app
