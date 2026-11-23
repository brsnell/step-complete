#pragma once

#include "core/AssemblyIndex.h"
#include "core/Clash.h"
#include "core/StepImporter.h"

#include <QObject>
#include <QString>

#include <memory>
#include <vector>

class QThread;

namespace sc::app {

// Runs core::findClashes on a worker thread (Measure > Check Clashes). Holds the model's index,
// so the document stays alive until the check ends. Signals are delivered on the thread that owns
// the job (the UI thread).
class ClashJob : public QObject {
  Q_OBJECT

public:
  ClashJob(std::shared_ptr<const core::AssemblyIndex> index, std::vector<core::NodeId> parts,
           core::ClashOptions options, QObject* parent = nullptr);
  ~ClashJob() override; // cancels and waits for the worker

  void start();
  void cancel() { _cancel.cancel(); }

  const std::shared_ptr<const core::AssemblyIndex>& index() const { return _index; }
  const core::ClashResult& result() const { return _result; }

Q_SIGNALS:
  void progress(int percent, const QString& stage);
  void finished();

private:
  std::shared_ptr<const core::AssemblyIndex> _index;
  std::vector<core::NodeId> _parts;
  core::ClashOptions _options;
  core::CancelToken _cancel;
  QThread* _thread = nullptr;
  core::ClashResult _result;
};

} // namespace sc::app
