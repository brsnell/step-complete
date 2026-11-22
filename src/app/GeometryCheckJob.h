#pragma once

#include "core/AssemblyIndex.h"
#include "core/GeometryCheck.h"
#include "core/StepImporter.h"

#include <QObject>
#include <QString>

#include <memory>
#include <vector>

class QThread;

namespace sc::app {

// Runs core::checkGeometry on a worker thread (Measure > Check Geometry). Holds the model's index,
// so the document stays alive until the check ends. Signals are delivered on the thread that owns
// the job (the UI thread).
class GeometryCheckJob : public QObject {
  Q_OBJECT

public:
  GeometryCheckJob(std::shared_ptr<const core::AssemblyIndex> index, std::vector<core::NodeId> parts,
                   QObject* parent = nullptr);
  ~GeometryCheckJob() override; // cancels and waits for the worker

  void start();
  void cancel() { _cancel.cancel(); }

  const std::shared_ptr<const core::AssemblyIndex>& index() const { return _index; }
  const core::GeometryCheckResult& result() const { return _result; }

Q_SIGNALS:
  void progress(int percent, const QString& stage);
  void finished();

private:
  std::shared_ptr<const core::AssemblyIndex> _index;
  std::vector<core::NodeId> _parts;
  core::CancelToken _cancel;
  QThread* _thread = nullptr;
  core::GeometryCheckResult _result;
};

} // namespace sc::app
