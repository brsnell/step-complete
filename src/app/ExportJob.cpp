#include "app/ExportJob.h"

#include "app/CrashHandler.h"

#include "core/Memory.h"

#include <QThread>

#include <cmath>

namespace sc::app {

ExportJob::ExportJob(std::shared_ptr<const core::AssemblyIndex> index, std::vector<core::NodeId> parts,
                     QString path, core::GeometryExportOptions options, QObject* parent)
    : QObject(parent), _index(std::move(index)), _parts(std::move(parts)), _path(std::move(path)),
      _options(options) {}

ExportJob::~ExportJob() {
  if (_thread != nullptr) {
    _cancel.cancel();
    _thread->wait();
  }
}

void ExportJob::start() {
  const std::string utf8Path = _path.toStdString();
  _thread = QThread::create([this, utf8Path] {
    prepareCrashHandlerThread(); // docs/designs/crash-reporting.md
    const auto onProgress = [this](double fraction, std::string_view stage) {
      const int percent = static_cast<int>(std::lround(fraction * 100.0));
      const QString text = QString::fromUtf8(stage.data(), static_cast<qsizetype>(stage.size()));
      QMetaObject::invokeMethod(
          this, [this, percent, text] { Q_EMIT progress(percent, text); }, Qt::QueuedConnection);
    };
    auto result = std::make_shared<core::GeometryExportResult>(
        core::exportGeometry(*_index, _parts, utf8Path, _options, onProgress, &_cancel));
    core::releaseFreedMemory(); // a finer export's copy of the model is gone by now
    QMetaObject::invokeMethod(
        this,
        [this, result] {
          _result = std::move(*result);
          Q_EMIT finished();
        },
        Qt::QueuedConnection);
  });
  _thread->setObjectName(QStringLiteral("Geometry export"));
  connect(_thread, &QThread::finished, _thread, &QObject::deleteLater);
  connect(_thread, &QObject::destroyed, this, [this] { _thread = nullptr; });
  _thread->start();
}

} // namespace sc::app
