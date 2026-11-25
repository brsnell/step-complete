#include "app/CrashHandler.h"
#include "app/CrashReports.h"
#include "app/MainWindow.h"

#include "core/Version.h"
#include "render/OcctViewport.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileOpenEvent>
#include <QIcon>
#include <QStringList>
#include <QTimer>

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
    _handler = std::move(handler);
    const QStringList queued = std::exchange(_queued, {});
    for (const QString& path : queued) {
      _handler(path);
    }
  }

protected:
  bool event(QEvent* event) override {
    if (event->type() == QEvent::FileOpen) {
      const QString path = static_cast<QFileOpenEvent*>(event)->file();
      if (!path.isEmpty()) {
        if (_handler) {
          _handler(path);
        } else {
          _queued.append(path);
        }
      }
      return true;
    }
    return QApplication::event(event);
  }

private:
  std::function<void(const QString&)> _handler;
  QStringList _queued;
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
    std::printf("%s %s\n", sc::core::AppName, sc::core::AppVersion);
    return 0;
  }
  configureGraphics(hasArgument(argc, argv, "--software-gl"));

  StepApplication app(argc, argv);
  QApplication::setWindowIcon(loadAppIcon());
  QApplication::setApplicationName(QString::fromUtf8(sc::core::AppName));
  QApplication::setApplicationVersion(QString::fromUtf8(sc::core::AppVersion));
  QApplication::setOrganizationName(QStringLiteral("STEP Complete"));

  QCommandLineParser parser;
  parser.setApplicationDescription(QString::fromUtf8(sc::core::AppDescription));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption({QStringLiteral("software-gl"),
                    QStringLiteral("Use software OpenGL rendering (Linux; for old or broken GPU drivers).")});
  parser.addOption({QStringLiteral("no-crash-handler"),
                    QStringLiteral("Do not write crash reports (for debuggers and sanitizers).")});
  parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("STEP file to open."),
                               QStringLiteral("[file]"));
  parser.process(app);

  // Crash reports (docs/designs/crash-reporting.md): written locally, shown on the next start.
  sc::app::CrashReports crashes(sc::app::CrashReports::defaultFolder());
  if (!parser.isSet(QStringLiteral("no-crash-handler"))) {
    sc::app::setCrashEnvironment(sc::app::defaultCrashEnvironment());
    sc::app::installCrashHandler(crashes.folder());
  }
  crashes.prune();

  sc::app::MainWindow window;
  window.show();
  const QStringList newCrashes = crashes.newReports();
  if (!newCrashes.isEmpty()) {
    crashes.markSeen(newCrashes.constFirst()); // shown once, even if this start crashes too
    QTimer::singleShot(0, &window, [&window, newCrashes] {
      sc::app::CrashReportDialog dialog(newCrashes.constFirst(), static_cast<int>(newCrashes.size()) - 1,
                                        &window);
      dialog.exec();
    });
  }
  if (!parser.positionalArguments().isEmpty()) {
    window.openFileWhenReady(parser.positionalArguments().constFirst());
  }
  // Also flushes file-open events that arrived while the window was being created.
  app.setOpenHandler([&window](const QString& path) { window.openFileWhenReady(path); });
  return QApplication::exec();
}
