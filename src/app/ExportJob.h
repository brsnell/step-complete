#pragma once

#include "core/AssemblyIndex.h"
#include "core/GeometryExport.h"
#include "core/StepImporter.h"

#include <QObject>
#include <QString>

#include <memory>
#include <vector>

class QThread;

namespace sc::app {

// Runs core::exportGeometry on a worker thread (File > Export Geometry). Holds the model's index,
// so the document stays alive until the export ends. Signals are delivered on the thread that
// owns the job (the UI thread).
class ExportJob : public QObject {
  Q_OBJECT

public:
  ExportJob(std::shared_ptr<const core::AssemblyIndex> index, std::vector<core::NodeId> parts, QString path,
            core::GeometryExportOptions options, QObject* parent = nullptr);
  ~ExportJob() override; // cancels and waits for the worker

  void start();
  void cancel() { _cancel.cancel(); }

  const QString& path() const { return _path; }
  const core::GeometryExportOptions& options() const { return _options; }
  int partCount() const { return static_cast<int>(_parts.size()); }
  const core::GeometryExportResult& result() const { return _result; }

Q_SIGNALS:
  void progress(int percent, const QString& stage);
  void finished();

private:
  std::shared_ptr<const core::AssemblyIndex> _index;
  std::vector<core::NodeId> _parts;
  QString _path;
  core::GeometryExportOptions _options;
  core::CancelToken _cancel;
  QThread* _thread = nullptr;
  core::GeometryExportResult _result;
};

} // namespace sc::app
