#include "app/ThicknessJob.h"

#include "app/CrashHandler.h"

#include <QThread>

#include <cmath>

namespace sc::app {

ThicknessJob::ThicknessJob(std::shared_ptr<const core::AssemblyIndex> index, std::vector<core::NodeId> parts,
                           QObject* parent)
    : QObject(parent), _index(std::move(index)), _parts(std::move(parts)) {}

ThicknessJob::~ThicknessJob() {
  if (_thread != nullptr) {
    _cancel.cancel();
    _thread->wait();
  }
}

void ThicknessJob::start() {
  _thread = QThread::create([this] {
    prepareCrashHandlerThread(); // docs/designs/crash-reporting.md
    const auto onProgress = [this](double fraction, std::string_view stage) {
      const int percent = static_cast<int>(std::lround(fraction * 100.0));
      const QString text = QString::fromUtf8(stage.data(), static_cast<qsizetype>(stage.size()));
      QMetaObject::invokeMethod(
          this, [this, percent, text] { Q_EMIT progress(percent, text); }, Qt::QueuedConnection);
    };
    std::shared_ptr<const core::ThicknessResult> result = std::make_shared<const core::ThicknessResult>(
        core::analyseThickness(*_index, _parts, {}, onProgress, &_cancel));
    QMetaObject::invokeMethod(
        this,
        [this, result] {
          _result = result;
          Q_EMIT finished();
        },
        Qt::QueuedConnection);
  });
  _thread->setObjectName(QStringLiteral("Wall thickness"));
  connect(_thread, &QThread::finished, _thread, &QObject::deleteLater);
  connect(_thread, &QObject::destroyed, this, [this] { _thread = nullptr; });
  _thread->start();
}

} // namespace sc::app
