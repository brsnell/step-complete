#pragma once

#include "core/AssemblyIndex.h"
#include "core/StepImporter.h"
#include "core/WallThickness.h"

#include <QObject>
#include <QString>

#include <memory>
#include <vector>

class QThread;

namespace sc::app {

// Runs core::analyseThickness on a worker thread (Measure > Wall Thickness). Holds the model's index,
// so the document stays alive until the check ends. Signals are delivered on the thread that owns
// the job (the UI thread).
class ThicknessJob : public QObject {
  Q_OBJECT

public:
  ThicknessJob(std::shared_ptr<const core::AssemblyIndex> index, std::vector<core::NodeId> parts,
               QObject* parent = nullptr);
  ~ThicknessJob() override; // cancels and waits for the worker

  void start();
  void cancel() { _cancel.cancel(); }

  const std::shared_ptr<const core::AssemblyIndex>& index() const { return _index; }
  // Shared, so the 3D view can keep the maps after the job is gone.
  const std::shared_ptr<const core::ThicknessResult>& result() const { return _result; }

Q_SIGNALS:
  void progress(int percent, const QString& stage);
  void finished();

private:
  std::shared_ptr<const core::AssemblyIndex> _index;
  std::vector<core::NodeId> _parts;
  core::CancelToken _cancel;
  QThread* _thread = nullptr;
  std::shared_ptr<const core::ThicknessResult> _result;
};

} // namespace sc::app
