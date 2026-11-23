#include "app/ComparisonJob.h"

#include "app/ImportJob.h"
#include "core/Memory.h"

#include <QThread>

#include <cmath>

namespace sc::app {

ComparisonJob::ComparisonJob(std::shared_ptr<const core::AssemblyIndex> current, QString otherPath,
                             std::shared_ptr<const core::AssemblyIndex> other,
                             core::ComparisonOptions options, QObject* parent)
    : QObject(parent), _current(std::move(current)), _otherPath(std::move(otherPath)),
      _other(std::move(other)), _options(options) {}

ComparisonJob::~ComparisonJob() {
  if (_thread != nullptr) {
    _cancel.cancel();
    _thread->wait();
  }
}

void ComparisonJob::start() {
  core::ImportOptions importOptions; // as File > Open, so both share the open-file cache
  if (ImportJob::cacheEnabled()) {
    importOptions.cacheDir = ImportJob::documentCacheDir().toStdString();
  }
  const std::string utf8Path = _otherPath.toStdString();
  _thread = QThread::create([this, utf8Path, importOptions] {
    // Importing is the first half of the bar, comparing the second (all of it when the other
    // revision is already loaded).
    const bool import = !_other;
    const auto report = [this](double fraction, std::string_view stage) {
      const int percent = static_cast<int>(std::lround(fraction * 100.0));
      const QString text = QString::fromUtf8(stage.data(), static_cast<qsizetype>(stage.size()));
      QMetaObject::invokeMethod(
          this, [this, percent, text] { Q_EMIT progress(percent, text); }, Qt::QueuedConnection);
    };
    std::shared_ptr<const core::AssemblyIndex> other = _other;
    std::shared_ptr<const core::ComparisonResult> result;
    std::string error;
    bool cancelled = false;
    if (import) {
      const core::ImportResult imported = core::importModel(
          utf8Path, importOptions, [&report](double f, std::string_view stage) { report(0.5 * f, stage); },
          &_cancel);
      cancelled = imported.cancelled;
      if (imported.ok) {
        other = std::make_shared<core::AssemblyIndex>(imported.document);
      } else if (!cancelled) {
        error = imported.error;
      }
    }
    if (other && !cancelled) {
      const double start = import ? 0.5 : 0.0;
      auto compared = std::make_shared<core::ComparisonResult>(core::compareModels(
          *_current, *other, _options,
          [&report, start](double f, std::string_view stage) { report(start + (1.0 - start) * f, stage); },
          &_cancel));
      cancelled = compared->cancelled;
      result = std::move(compared);
    }
    core::releaseFreedMemory();
    QMetaObject::invokeMethod(
        this,
        [this, other, result, error, cancelled] {
          _other = other;
          _result = result;
          _error = error;
          _cancelled = cancelled;
          Q_EMIT finished();
        },
        Qt::QueuedConnection);
  });
  _thread->setObjectName(QStringLiteral("Model comparison"));
  connect(_thread, &QThread::finished, _thread, &QObject::deleteLater);
  connect(_thread, &QObject::destroyed, this, [this] { _thread = nullptr; });
  _thread->start();
}

} // namespace sc::app
