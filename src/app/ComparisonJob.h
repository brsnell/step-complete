#pragma once

#include "core/AssemblyIndex.h"
#include "core/Comparison.h"
#include "core/StepImporter.h"

#include <QObject>
#include <QString>

#include <memory>
#include <string>

class QThread;

namespace sc::app {

// File > Compare With (docs/designs/model-comparison.md): on a worker thread, imports the other
// revision (unless it is given, to compare again with another tolerance) and compares the current
// model with it. Holds both indexes, so the documents stay alive until the job ends. Signals are
// delivered on the thread that owns the job (the UI thread).
class ComparisonJob : public QObject {
  Q_OBJECT

public:
  ComparisonJob(std::shared_ptr<const core::AssemblyIndex> current, QString otherPath,
                std::shared_ptr<const core::AssemblyIndex> other, core::ComparisonOptions options,
                QObject* parent = nullptr);
  ~ComparisonJob() override; // cancels and waits for the worker

  void start();
  void cancel() { _cancel.cancel(); }

  const std::shared_ptr<const core::AssemblyIndex>& current() const { return _current; }
  const QString& otherPath() const { return _otherPath; }
  const core::ComparisonOptions& options() const { return _options; }
  // After finished(): the other revision, the result, or the reason the other file did not open.
  const std::shared_ptr<const core::AssemblyIndex>& other() const { return _other; }
  const std::shared_ptr<const core::ComparisonResult>& result() const { return _result; }
  const std::string& error() const { return _error; }
  bool cancelled() const { return _cancelled; }

Q_SIGNALS:
  void progress(int percent, const QString& stage);
  void finished();

private:
  std::shared_ptr<const core::AssemblyIndex> _current;
  QString _otherPath;
  std::shared_ptr<const core::AssemblyIndex> _other;
  core::ComparisonOptions _options;
  core::CancelToken _cancel;
  QThread* _thread = nullptr;
  std::shared_ptr<const core::ComparisonResult> _result;
  std::string _error;
  bool _cancelled = false;
};

} // namespace sc::app
