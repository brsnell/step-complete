#include "app/ImportJob.h"

#include "app/CrashHandler.h"

#include "core/Memory.h"

#include <QSettings>
#include <QStandardPaths>
#include <QThread>

#include <cmath>

namespace sc::app {

ImportJob::ImportJob(QString path, QObject* parent) : QObject(parent), _path(std::move(path)) {}

ImportJob::~ImportJob() {
  if (_thread != nullptr) {
    _cancel.cancel();
    _thread->wait();
  }
}

QString ImportJob::documentCacheDir() {
  const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  return cacheRoot.isEmpty() ? QString() : cacheRoot + QStringLiteral("/documents");
}

bool ImportJob::cacheEnabled() {
  return QSettings().value(QStringLiteral("cache/enabled"), true).toBool();
}

void ImportJob::setCacheEnabled(bool enabled) {
  QSettings().setValue(QStringLiteral("cache/enabled"), enabled);
}

void ImportJob::start() {
  _timer.start();
  const std::string utf8Path = _path.toStdString();

  // Open-file cache; the "cache/enabled" setting (default on) is read on the UI thread.
  core::ImportOptions options;
  if (cacheEnabled()) {
    options.cacheDir = documentCacheDir().toStdString();
  }

  _thread = QThread::create([this, utf8Path, options] {
    prepareCrashHandlerThread(); // docs/designs/crash-reporting.md
    const auto onProgress = [this](double fraction, std::string_view stage) {
      const int percent = static_cast<int>(std::lround(fraction * 100.0));
      const QString text = QString::fromUtf8(stage.data(), static_cast<qsizetype>(stage.size()));
      QMetaObject::invokeMethod(
          this, [this, percent, text] { Q_EMIT progress(percent, text); }, Qt::QueuedConnection);
    };

    auto result =
        std::make_shared<core::ImportResult>(core::importModel(utf8Path, options, onProgress, &_cancel));
    std::shared_ptr<const core::AssemblyIndex> index;
    std::shared_ptr<const core::PmiModel> pmi;
    if (result->ok) {
      index = std::make_shared<core::AssemblyIndex>(result->document);
      pmi = std::make_shared<core::PmiModel>(result->document);
    }
    core::releaseFreedMemory(); // the STEP reader's model is gone by now

    QMetaObject::invokeMethod(
        this,
        [this, result, index, pmi] {
          _result = std::move(*result);
          _index = index;
          _pmi = pmi;
          _elapsedSeconds = _timer.elapsed() / 1000.0;
          Q_EMIT finished();
        },
        Qt::QueuedConnection);
  });
  _thread->setObjectName(QStringLiteral("STEP import"));
  connect(_thread, &QThread::finished, _thread, &QObject::deleteLater);
  connect(_thread, &QObject::destroyed, this, [this] { _thread = nullptr; });
  _thread->start();
}

} // namespace sc::app
