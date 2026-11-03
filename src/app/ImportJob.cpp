#include "app/ImportJob.h"

#include <QSettings>
#include <QStandardPaths>
#include <QThread>

#include <cmath>

namespace sc::app {

ImportJob::ImportJob(QString path, QObject* parent) : QObject(parent), m_path(std::move(path)) {}

ImportJob::~ImportJob() {
  if (m_thread != nullptr) {
    m_cancel.cancel();
    m_thread->wait();
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
  m_timer.start();
  const std::string utf8Path = m_path.toStdString();

  // Open-file cache; the "cache/enabled" setting (default on) is read on the UI thread.
  core::ImportOptions options;
  if (cacheEnabled()) {
    options.cacheDir = documentCacheDir().toStdString();
  }

  m_thread = QThread::create([this, utf8Path, options] {
    const auto onProgress = [this](double fraction, std::string_view stage) {
      const int percent = static_cast<int>(std::lround(fraction * 100.0));
      const QString text = QString::fromUtf8(stage.data(), static_cast<qsizetype>(stage.size()));
      QMetaObject::invokeMethod(
          this, [this, percent, text] { Q_EMIT progress(percent, text); }, Qt::QueuedConnection);
    };

    auto result =
        std::make_shared<core::ImportResult>(core::importStep(utf8Path, options, onProgress, &m_cancel));
    std::shared_ptr<const core::AssemblyIndex> index;
    std::shared_ptr<const core::PmiModel> pmi;
    if (result->ok) {
      index = std::make_shared<core::AssemblyIndex>(result->document);
      pmi = std::make_shared<core::PmiModel>(result->document);
    }

    QMetaObject::invokeMethod(
        this,
        [this, result, index, pmi] {
          m_result = std::move(*result);
          m_index = index;
          m_pmi = pmi;
          m_elapsedSeconds = m_timer.elapsed() / 1000.0;
          Q_EMIT finished();
        },
        Qt::QueuedConnection);
  });
  m_thread->setObjectName(QStringLiteral("STEP import"));
  connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
  connect(m_thread, &QObject::destroyed, this, [this] { m_thread = nullptr; });
  m_thread->start();
}

} // namespace sc::app
