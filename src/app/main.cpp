#include "app/MainWindow.h"

#include "core/Version.h"
#include "render/OcctViewport.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileOpenEvent>
#include <QIcon>
#include <QStringList>

#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>

namespace {

// Finder (and "Open With" on macOS) delivers documents as QFileOpenEvent instead of argv.
// Events can arrive before the main window exists, so they are queued until a handler is set.
class StepApplication : public QApplication {
public:
  using QApplication::QApplication;

  void setOpenHandler(std::function<void(const QString&)> handler) {
    m_handler = std::move(handler);
    const QStringList queued = std::exchange(m_queued, {});
    for (const QString& path : queued) {
      m_handler(path);
    }
  }

protected:
  bool event(QEvent* e) override {
    if (e->type() == QEvent::FileOpen) {
      const QString path = static_cast<QFileOpenEvent*>(e)->file();
      if (!path.isEmpty()) {
        if (m_handler) {
          m_handler(path);
        } else {
          m_queued.append(path);
        }
      }
      return true;
    }
    return QApplication::event(e);
  }

private:
  std::function<void(const QString&)> m_handler;
  QStringList m_queued;
};

QIcon loadAppIcon() {
  QIcon icon;
  for (const int size : {16, 24, 32, 48, 64, 128, 256, 512}) {
    icon.addFile(QStringLiteral(":/icons/stepcomplete-%1.png").arg(size), QSize(size, size));
  }
  return icon;
}

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
  // Answer --version without needing a display (scripts, packaging checks).
  if (hasArgument(argc, argv, "--version") || hasArgument(argc, argv, "-v")) {
    std::printf("%s %s\n", sc::core::kAppName, sc::core::kAppVersion);
    return 0;
  }
  configureGraphics(hasArgument(argc, argv, "--software-gl"));

  StepApplication app(argc, argv);
  QApplication::setWindowIcon(loadAppIcon());
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
  // Also flushes file-open events that arrived while the window was being created.
  app.setOpenHandler([&window](const QString& path) { window.openFileWhenReady(path); });
  return QApplication::exec();
}
