#include "app/MainWindow.h"

#include "core/Version.h"
#include "render/OcctViewport.h"

#include <QApplication>
#include <QCommandLineParser>

#include <cstring>

namespace {

bool hasArgument(int argc, char** argv, const char* name) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], name) == 0) {
      return true;
    }
  }
  return false;
}

// Graphics setup that must happen before QApplication exists.
void configureGraphics(bool softwareGl) {
#if defined(__linux__)
  // OCCT's Linux OpenGL driver is built on GLX, which cannot wrap the EGL contexts
  // Qt uses on native Wayland. Run through X11/XWayland unless the user overrides it.
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "xcb");
  }
  if (qEnvironmentVariableIsEmpty("QT_XCB_GL_INTEGRATION")) {
    qputenv("QT_XCB_GL_INTEGRATION", "xcb_glx");
  }
  if (softwareGl) {
    qputenv("LIBGL_ALWAYS_SOFTWARE", "1"); // Mesa llvmpipe
  }
#elif defined(_WIN32)
  // Never use ANGLE/GLES: OCCT talks to desktop OpenGL directly.
  QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
  Q_UNUSED(softwareGl); // Windows software rendering is planned for M5 (bundled Mesa).
#else
  Q_UNUSED(softwareGl);
#endif
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  sc::render::OcctViewport::configureDefaultSurfaceFormat();
}

} // namespace

int main(int argc, char** argv) {
  configureGraphics(hasArgument(argc, argv, "--software-gl"));

  QApplication app(argc, argv);
  QApplication::setApplicationName(QString::fromUtf8(sc::core::kAppName));
  QApplication::setApplicationVersion(QString::fromUtf8(sc::core::kAppVersion));
  QApplication::setOrganizationName(QStringLiteral("STEP Complete"));

  QCommandLineParser parser;
  parser.setApplicationDescription(QString::fromUtf8(sc::core::kAppDescription));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption({QStringLiteral("software-gl"),
                    QStringLiteral("Use software OpenGL rendering (Linux; for old or broken GPU drivers).")});
  parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("STEP file to open."),
                               QStringLiteral("[file]"));
  parser.process(app);

  sc::app::MainWindow window;
  window.show();
  if (!parser.positionalArguments().isEmpty()) {
    window.openFileWhenReady(parser.positionalArguments().constFirst());
  }
  return QApplication::exec();
}
