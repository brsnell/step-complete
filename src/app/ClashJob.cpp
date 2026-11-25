#include "app/ClashJob.h"

#include "app/CrashHandler.h"

#include <QThread>

#include <cmath>

namespace sc::app {

ClashJob::ClashJob(std::shared_ptr<const core::AssemblyIndex> index, std::vector<core::NodeId> parts,
                   core::ClashOptions options, QObject* parent)
    : QObject(parent), _index(std::move(index)), _parts(std::move(parts)), _options(std::move(options)) {}

ClashJob::~ClashJob() {
  if (_thread != nullptr) {
    _cancel.cancel();
    _thread->wait();
  }
}

void ClashJob::start() {
  _thread = QThread::create([this] {
    prepareCrashHandlerThread(); // docs/designs/crash-reporting.md
    const auto onProgress = [this](double fraction, std::string_view stage) {
      const int percent = static_cast<int>(std::lround(fraction * 100.0));
      const QString text = QString::fromUtf8(stage.data(), static_cast<qsizetype>(stage.size()));
      QMetaObject::invokeMethod(
          this, [this, percent, text] { Q_EMIT progress(percent, text); }, Qt::QueuedConnection);
    };
    auto result = std::make_shared<core::ClashResult>(
        core::findClashes(*_index, _parts, _options, onProgress, &_cancel));
    QMetaObject::invokeMethod(
        this,
        [this, result] {
          _result = std::move(*result);
          Q_EMIT finished();
        },
        Qt::QueuedConnection);
  });
  _thread->setObjectName(QStringLiteral("Clash check"));
  connect(_thread, &QThread::finished, _thread, &QObject::deleteLater);
  connect(_thread, &QObject::destroyed, this, [this] { _thread = nullptr; });
  _thread->start();
}

} // namespace sc::app
