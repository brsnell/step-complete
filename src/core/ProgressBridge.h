#pragma once

#include "core/StepImporter.h"

#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressScope.hxx>

#include <chrono>
#include <string>

namespace sc::core {

// Bridges OCCT's progress reporting to our callback and cancel token (internal to sc::core).
// Reports are throttled to one per 50 ms unless forced.
class ProgressBridge : public Message_ProgressIndicator {
public:
  ProgressBridge(const ProgressCallback& callback, const CancelToken* cancel)
      : _callback(callback), _cancel(cancel) {}

  void setStage(std::string stage) {
    _stage = std::move(stage);
    report(GetPosition(), true);
  }

  bool UserBreak() override { return _cancel != nullptr && _cancel->isCancelled(); }

  void Show(const Message_ProgressScope&, const bool isForce) override { report(GetPosition(), isForce); }

  void report(double fraction, bool force) {
    if (!_callback) {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!force && now - _lastReport < std::chrono::milliseconds(50)) {
      return;
    }
    _lastReport = now;
    _callback(fraction, _stage);
  }

private:
  const ProgressCallback& _callback;
  const CancelToken* _cancel;
  std::string _stage;
  std::chrono::steady_clock::time_point _lastReport{};
};

} // namespace sc::core
