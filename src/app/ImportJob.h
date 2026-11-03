#pragma once

#include "core/AssemblyIndex.h"
#include "core/PmiModel.h"
#include "core/StepImporter.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <memory>

class QThread;

namespace sc::app {

// Imports a STEP file on a worker thread and builds its assembly index and PMI model.
// Signals are delivered on the thread that owns the job (the UI thread).
class ImportJob : public QObject {
  Q_OBJECT

public:
  explicit ImportJob(QString path, QObject* parent = nullptr);
  ~ImportJob() override; // cancels and waits for the worker

  // Where opened files are cached (empty if the platform has no cache location),
  // and whether caching is on (QSettings "cache/enabled", default true).
  static QString documentCacheDir();
  static bool cacheEnabled();
  static void setCacheEnabled(bool enabled);

  void start();
  void cancel() { m_cancel.cancel(); }

  const QString& path() const { return m_path; }
  const core::ImportResult& result() const { return m_result; }
  const std::shared_ptr<const core::AssemblyIndex>& index() const { return m_index; }
  const std::shared_ptr<const core::PmiModel>& pmi() const { return m_pmi; }
  double elapsedSeconds() const { return m_elapsedSeconds; }

Q_SIGNALS:
  void progress(int percent, const QString& stage);
  void finished();

private:
  QString m_path;
  core::CancelToken m_cancel;
  QThread* m_thread = nullptr;
  QElapsedTimer m_timer;
  core::ImportResult m_result;
  std::shared_ptr<const core::AssemblyIndex> m_index;
  std::shared_ptr<const core::PmiModel> m_pmi;
  double m_elapsedSeconds = 0.0;
};

} // namespace sc::app
