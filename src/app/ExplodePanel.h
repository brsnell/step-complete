#pragma once

#include <QWidget>

class QLabel;
class QSlider;
class QToolButton;

namespace sc::app {

// The Exploded View dock (View > Exploded View, docs/designs/exploded-view.md): how far the parts
// move apart, and Collapse.
class ExplodePanel : public QWidget {
  Q_OBJECT

public:
  static constexpr int DefaultDistance = 50; // percent

  explicit ExplodePanel(QWidget* parent = nullptr);

  // 0 to 100 %.
  int distance() const;
  // Sets the slider without emitting distanceChanged.
  void setDistance(int percent);
  // Whether the model is exploded now: Collapse is only enabled then.
  void setExploded(bool exploded);

Q_SIGNALS:
  // The user moved the slider.
  void distanceChanged(int percent);
  void collapseRequested();

private:
  void showDistance();

  QSlider* _slider = nullptr;
  QLabel* _value = nullptr;
  QToolButton* _collapse = nullptr;
};

} // namespace sc::app
