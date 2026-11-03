// Times a STEP import the way the app does it (read, transfer, premesh, assembly
// index, PMI model) and reports memory. Usage: sc_bench_import <file.stp>
#include "core/AssemblyIndex.h"
#include "core/PmiModel.h"
#include "core/StepImporter.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

namespace {

// Resident and peak resident set size in MB (Linux only; -1 elsewhere).
void memoryMb(double& rss, double& peak) {
  rss = peak = -1.0;
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    if (line.rfind("VmRSS:", 0) == 0) {
      rss = std::stod(line.substr(6)) / 1024.0;
    } else if (line.rfind("VmHWM:", 0) == 0) {
      peak = std::stod(line.substr(6)) / 1024.0;
    }
  }
}

double secondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s file.stp\n", argv[0]);
    return 2;
  }
  const auto start = std::chrono::steady_clock::now();
  sc::core::ImportResult result = sc::core::importStep(argv[1]);
  if (!result.ok) {
    std::fprintf(stderr, "import failed: %s\n", result.error.c_str());
    return 1;
  }
  const double importSeconds = secondsSince(start);
  const auto indexStart = std::chrono::steady_clock::now();
  const auto index = std::make_shared<sc::core::AssemblyIndex>(result.document);
  const auto pmi = std::make_shared<sc::core::PmiModel>(result.document);
  const double indexSeconds = secondsSince(indexStart);

  double rss = 0.0, peak = 0.0;
  memoryMb(rss, peak);
  std::printf("read %.2fs  transfer %.2fs  mesh %.2fs  index+pmi %.2fs  total %.2fs\n",
              result.stats.readSeconds, result.stats.transferSeconds, result.stats.meshSeconds, indexSeconds,
              importSeconds + indexSeconds);
  std::printf("roots %d  unique parts %d  nodes %zu  pmi %zu\n", result.stats.rootCount,
              result.stats.uniqueParts, index->nodes().size(), pmi->items().size());
  std::printf("memory: rss %.0f MB  peak %.0f MB\n", rss, peak);
  return 0;
}
