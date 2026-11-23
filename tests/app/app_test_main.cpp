// Shared main for the app widget tests: runs every test class in turn, offscreen
// (no display or OpenGL needed).

#include <QApplication>

#include <cstdio>

int runPmiPanelTests(int argc, char** argv);
int runSectionPanelTests(int argc, char** argv);
int runAboutDialogTests(int argc, char** argv);
int runMeasurementsPanelTests(int argc, char** argv);
int runAssemblyFilterTests(int argc, char** argv);
int runExportGeometryDialogTests(int argc, char** argv);
int runUserViewsTests(int argc, char** argv);
int runMaterialDialogTests(int argc, char** argv);
int runPartsListDialogTests(int argc, char** argv);
int runGeometryCheckPanelTests(int argc, char** argv);
int runClashPanelTests(int argc, char** argv);

int main(int argc, char** argv) {
  // Keep a failure readable in CI logs. Unbuffered output survives a crash or abort (on Windows a
  // pipe is fully buffered), and without a console Qt on Windows would send its own warnings and
  // fatal messages to the debugger instead of stderr.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (qEnvironmentVariableIsEmpty("QT_FORCE_STDERR_LOGGING")) {
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
  }
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  int failures = 0;
  failures += runPmiPanelTests(argc, argv);
  failures += runSectionPanelTests(argc, argv);
  failures += runAboutDialogTests(argc, argv);
  failures += runMeasurementsPanelTests(argc, argv);
  failures += runAssemblyFilterTests(argc, argv);
  failures += runExportGeometryDialogTests(argc, argv);
  failures += runUserViewsTests(argc, argv);
  failures += runMaterialDialogTests(argc, argv);
  failures += runPartsListDialogTests(argc, argv);
  failures += runGeometryCheckPanelTests(argc, argv);
  failures += runClashPanelTests(argc, argv);
  return failures == 0 ? 0 : 1;
}
