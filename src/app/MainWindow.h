#pragma once

#include <QMainWindow>

class QAction;
class QActionGroup;
class QLabel;

namespace sc::render {
class OcctViewport;
}

namespace sc::app {

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);

private:
  void createActions();
  void onGlInitialized(bool ok, const QString& message);
  void showGraphicsInfo();
  void showAbout();

  render::OcctViewport* m_viewport = nullptr;
  QLabel* m_glStatus = nullptr;
  QActionGroup* m_styleGroup = nullptr;
  bool m_demoShown = false;
};

} // namespace sc::app
