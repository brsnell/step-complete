// Shared main for the app widget tests: runs every test class in turn, offscreen
// (no display or OpenGL needed).

#include <QApplication>

int runPmiPanelTests(int argc, char** argv);
int runSectionPanelTests(int argc, char** argv);

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  int failures = 0;
  failures += runPmiPanelTests(argc, argv);
  failures += runSectionPanelTests(argc, argv);
  return failures == 0 ? 0 : 1;
}
