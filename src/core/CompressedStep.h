#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace sc::core {

// Container formats a STEP file may come in. Detected from magic bytes, never from the extension.
enum class Compression { None, Gzip, Zip };

// Looks at the first bytes of the file: gzip (1F 8B) or zip ("PK\x03\x04"; an empty zip starts with
// the end-of-central-directory record "PK\x05\x06"). Anything unreadable or unknown is None.
Compression detectCompression(const std::string& utf8Path);

struct DecompressResult {
  bool ok = false;
  bool cancelled = false;
  std::string error;       // set when !ok && !cancelled
  std::uint64_t bytes = 0; // uncompressed bytes written
};

struct DecompressOptions {
  // Refuse to write more than this many uncompressed bytes (protects against decompression bombs).
  std::uint64_t maxBytes = std::uint64_t(4) << 30;
  // Polled between chunks; return true to abort (result.cancelled).
  std::function<bool()> isCancelled;
  // Called between chunks with the fraction of the compressed input consumed, in [0, 1].
  std::function<void(double)> progress;
};

// Inflates a gzip file (all concatenated members) or the STEP entry of a zip archive (first entry
// named *.stp/*.step/*.p21, else the first file entry; stored or deflated) into 'utf8OutPath',
// streaming in chunks. On failure or cancellation the partial output file is removed.
DecompressResult decompressStepFile(const std::string& utf8InPath, Compression kind,
                                    const std::string& utf8OutPath, const DecompressOptions& options = {});

} // namespace sc::core
