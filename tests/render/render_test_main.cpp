// Entry point for all render tests: one QApplication, one GL configuration.

#include "RenderTestUtils.h"

#include "render/OcctViewport.h"

#include <QApplication>

int main(int argc, char** argv) {
#if defined(__linux__)
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "xcb");
  }
  if (qEnvironmentVariableIsEmpty("QT_XCB_GL_INTEGRATION")) {
    qputenv("QT_XCB_GL_INTEGRATION", "xcb_glx");
  }
#endif
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  sc::render::OcctViewport::configureDefaultSurfaceFormat();
  QApplication app(argc, argv);

  int failures = 0;
  failures += sc::test::runViewportSmokeTests(argc, argv);
  failures += sc::test::runSceneManagerTests(argc, argv);
  failures += sc::test::runMeasureToolTests(argc, argv);
  failures += sc::test::runPmiSceneTests(argc, argv);
  return failures == 0 ? 0 : 1;
}
