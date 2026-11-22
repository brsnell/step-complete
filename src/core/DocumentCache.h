#pragma once

#include "core/Document.h"
#include "core/StepImporter.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sc::core {

// Everything needed to show a model again without reading the STEP file.
struct CachedImport {
  DocumentPtr document;
  std::vector<ImportMessage> messages;
  int rootCount = 0;
  int uniqueParts = 0;
};

// Fast cache key for a file: size, modification time and a hash of the first and
// last megabyte plus sampled blocks in between (hashing a whole 500 MB file on a
// hard disk would defeat the purpose of a cache). It also covers the cache format,
// the app and OCCT versions and the mesh parameters used by the pre-mesher.
// 'variant' distinguishes import options that change the document's content.
// Returns an empty string if the file cannot be read.
std::string cacheKey(const std::string& utf8Path, std::string_view variant = {});

// A key for data the user keeps about a model (saved views, ...): the file's size and sampled
// content only, the same sampling as cacheKey. Unlike cacheKey it ignores the modification time
// and the app, OCCT and cache versions, so copies, moves, touches and upgrades keep it. Returns an
// empty string if the file cannot be read.
std::string fileFingerprint(const std::string& utf8Path);

// On-disk cache of imported documents: <dir>/<key>.cbf (the XCAF document as
// BinXCAF, including triangulations) and <dir>/<key>.meta (what lives outside
// OCAF: source path, file unit, cameras, import messages).
//
// Writes are atomic (temp file + rename). Corrupt or partial entries count as
// misses and are deleted. Like importStep, the OCAF calls are not thread-safe
// against other imports: call from one thread at a time.
class DocumentCache {
public:
  explicit DocumentCache(std::filesystem::path dir, std::uint64_t maxBytes = std::uint64_t{1} << 30);

  const std::filesystem::path& directory() const { return _dir; }

  // A hit refreshes the entry's modification time (LRU order for prune()).
  std::optional<CachedImport> load(const std::string& key) const;
  bool store(const std::string& key, const Document& document, const std::vector<ImportMessage>& messages,
             int rootCount, int uniqueParts) const;
  bool contains(const std::string& key) const;
  void remove(const std::string& key) const;

  // Deletes least recently used entries until the total size is <= maxBytes.
  void prune() const;

  std::uint64_t totalBytes() const;

private:
  std::filesystem::path entryPath(const std::string& key, const char* extension) const;

  std::filesystem::path _dir;
  std::uint64_t _maxBytes;
};

// A deep copy of the document made through BinXCAF in memory: the same labels (entries),
// names, colors and shapes, sharing nothing with the source. Without triangulations the copy's
// faces have no meshes. Returns null on failure. Opens a document in OCCT's shared XCAF
// application: hold xcafMutex() when other threads may import, and while the copy is destroyed.
DocumentPtr copyDocument(const Document& source, bool withTriangulations);

} // namespace sc::core
