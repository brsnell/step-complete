#pragma once

#include "core/Document.h"

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sc::core {

// Thread-safe cancellation flag shared between the UI and an import running on a worker.
class CancelToken {
public:
  void cancel() { m_cancelled.store(true, std::memory_order_relaxed); }
  bool isCancelled() const { return m_cancelled.load(std::memory_order_relaxed); }

private:
  std::atomic<bool> m_cancelled{false};
};

// Called from the importing thread. 'fraction' is in [0, 1]; 'stage' is a short label.
// Calls are throttled, so it is cheap to forward them to a UI thread.
using ProgressCallback = std::function<void(double fraction, std::string_view stage)>;

struct ImportOptions {
  bool readPmi = true;
  // Triangulate every unique part on the importing thread, so displaying the
  // model on the UI thread is fast. Uses the same parameters as the viewer.
  bool premesh = true;
};

struct ImportMessage {
  enum class Severity { Info, Warning, Error };
  Severity severity = Severity::Info;
  std::string text;
  int count = 1; // identical messages are merged
};

struct ImportStats {
  double readSeconds = 0.0;
  double transferSeconds = 0.0;
  double meshSeconds = 0.0;
  int rootCount = 0;
  int uniqueParts = 0;
};

struct ImportResult {
  DocumentPtr document; // null unless ok
  bool ok = false;
  bool cancelled = false;
  std::string error; // set when !ok && !cancelled
  std::vector<ImportMessage> messages;
  ImportStats stats;
};

// Reads a STEP file (AP203/AP214/AP242) into an XCAF document with names,
// colors, layers, validation properties, GD&T and saved views.
//
// Blocking; run it off the UI thread. Only one import may run at a time:
// OCCT's translator parameters are process-global.
ImportResult importStep(const std::string& utf8Path, const ImportOptions& options = {},
                        const ProgressCallback& progress = {}, const CancelToken* cancel = nullptr);

} // namespace sc::core
