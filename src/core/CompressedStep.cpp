#include "core/CompressedStep.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <vector>

namespace sc::core {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kChunk = 256 * 1024;

fs::path toPath(const std::string& utf8) {
  return fs::u8path(utf8);
}

std::uint32_t le16(const unsigned char* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8);
}

std::uint32_t le32(const unsigned char* p) {
  return le16(p) | (le16(p + 2) << 16);
}

// Writes inflated data, enforcing the size cap and polling cancellation.
class Sink {
public:
  Sink(std::ofstream& out, const DecompressOptions& options) : m_out(out), m_options(options) {}

  // False (with error set) when decompression must stop.
  bool write(const unsigned char* data, std::size_t size) {
    m_total += size;
    if (m_total > m_options.maxBytes) {
      m_error = "The compressed file expands to more than " + std::to_string(m_options.maxBytes >> 20) +
                " MB; refusing to open it.";
      return false;
    }
    m_out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!m_out) {
      m_error = "Could not write the temporary decompressed file (disk full?).";
      return false;
    }
    m_crc = crc32(m_crc, data, static_cast<uInt>(size));
    return true;
  }

  bool poll(double fraction) {
    if (m_options.isCancelled && m_options.isCancelled()) {
      m_cancelled = true;
      return false;
    }
    if (m_options.progress) {
      m_options.progress(std::clamp(fraction, 0.0, 1.0));
    }
    return true;
  }

  std::uint64_t total() const { return m_total; }
  std::uint32_t crc() const { return static_cast<std::uint32_t>(m_crc); }
  bool cancelled() const { return m_cancelled; }
  const std::string& error() const { return m_error; }
  void fail(std::string message) { m_error = std::move(message); }

private:
  std::ofstream& m_out;
  const DecompressOptions& m_options;
  std::uint64_t m_total = 0;
  uLong m_crc = crc32(0L, Z_NULL, 0);
  bool m_cancelled = false;
  std::string m_error;
};

// Inflates 'compressedSize' bytes starting at the stream's current position.
// windowBits: 15 + 32 for gzip/zlib auto-detect, -15 for raw deflate. 'multiMember' keeps going
// across concatenated gzip members. Returns true on a clean end of stream.
bool inflateStream(std::ifstream& in, std::uint64_t compressedSize, int windowBits, bool multiMember,
                   Sink& sink) {
  z_stream zs{};
  if (inflateInit2(&zs, windowBits) != Z_OK) {
    sink.fail("Could not initialize the decompressor.");
    return false;
  }
  std::vector<unsigned char> inBuf(kChunk), outBuf(kChunk);
  std::uint64_t remaining = compressedSize;
  const double denom = compressedSize > 0 ? static_cast<double>(compressedSize) : 1.0;
  bool ok = false;
  bool atMemberEnd = false; // the current member finished; zs is waiting for a reset

  while (true) {
    if (zs.avail_in == 0) {
      std::size_t got = 0;
      if (remaining > 0) {
        const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, inBuf.size()));
        in.read(reinterpret_cast<char*>(inBuf.data()), static_cast<std::streamsize>(want));
        got = static_cast<std::size_t>(in.gcount());
      }
      if (got == 0) {
        // Out of input: fine only between gzip members.
        ok = atMemberEnd;
        if (!ok) {
          sink.fail("The compressed data is truncated.");
        }
        break;
      }
      remaining -= got;
      zs.next_in = inBuf.data();
      zs.avail_in = static_cast<uInt>(got);
      if (!sink.poll(static_cast<double>(compressedSize - remaining) / denom)) {
        break;
      }
    }
    if (atMemberEnd) {
      // Another gzip member follows only if the gzip magic does; otherwise ignore trailing padding.
      if (zs.avail_in >= 2 && zs.next_in[0] == 0x1F && zs.next_in[1] == 0x8B) {
        inflateReset(&zs);
        atMemberEnd = false;
      } else {
        ok = true;
        break;
      }
    }
    zs.next_out = outBuf.data();
    zs.avail_out = static_cast<uInt>(outBuf.size());
    const int rc = inflate(&zs, Z_NO_FLUSH);
    const std::size_t produced = outBuf.size() - zs.avail_out;
    if (produced > 0 && !sink.write(outBuf.data(), produced)) {
      break;
    }
    if (rc == Z_STREAM_END) {
      if (!multiMember) {
        ok = true;
        break;
      }
      atMemberEnd = true;
    } else if (rc != Z_OK && rc != Z_BUF_ERROR) {
      sink.fail(std::string("The compressed data is corrupt") +
                (zs.msg != nullptr ? std::string(" (") + zs.msg + ")" : std::string()) + ".");
      break;
    }
    if (!sink.poll(static_cast<double>(compressedSize - remaining) / denom)) {
      break;
    }
  }
  inflateEnd(&zs);
  return ok;
}

bool endsWithNoCase(const std::string& name, const char* suffix) {
  const std::size_t n = std::strlen(suffix);
  if (name.size() < n) {
    return false;
  }
  for (std::size_t i = 0; i < n; ++i) {
    if (std::tolower(static_cast<unsigned char>(name[name.size() - n + i])) != suffix[i]) {
      return false;
    }
  }
  return true;
}

struct ZipEntry {
  std::string name;
  std::uint32_t flags = 0;
  std::uint32_t method = 0;
  std::uint32_t crc = 0;
  std::uint64_t compressedSize = 0;
  std::uint64_t size = 0;
  std::uint64_t localOffset = 0;
};

bool readBytes(std::ifstream& in, std::uint64_t offset, void* data, std::size_t size) {
  in.clear();
  in.seekg(static_cast<std::streamoff>(offset));
  in.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
  return static_cast<std::size_t>(in.gcount()) == size;
}

// Reads the central directory and picks the entry to extract. Empty error on success.
std::string findZipEntry(std::ifstream& in, std::uint64_t fileSize, ZipEntry& chosen) {
  const std::uint64_t tailSize = std::min<std::uint64_t>(fileSize, 22 + 65535);
  if (tailSize < 22) {
    return "The zip archive is truncated.";
  }
  std::vector<unsigned char> tail(static_cast<std::size_t>(tailSize));
  if (!readBytes(in, fileSize - tailSize, tail.data(), tail.size())) {
    return "The zip archive is truncated.";
  }
  std::size_t eocd = tail.size();
  for (std::size_t i = tail.size() - 22 + 1; i-- > 0;) {
    if (le32(&tail[i]) == 0x06054b50) {
      eocd = i;
      break;
    }
  }
  if (eocd == tail.size()) {
    return "The zip archive has no central directory.";
  }
  const std::uint32_t entryCount = le16(&tail[eocd + 10]);
  const std::uint32_t cdSize = le32(&tail[eocd + 12]);
  const std::uint32_t cdOffset = le32(&tail[eocd + 16]);
  if (entryCount == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOffset == 0xFFFFFFFFu) {
    return "ZIP64 archives are not supported.";
  }
  if (entryCount == 0) {
    return "The zip archive is empty.";
  }
  if (static_cast<std::uint64_t>(cdOffset) + cdSize > fileSize) {
    return "The zip archive is corrupt (bad central directory).";
  }
  std::vector<unsigned char> cd(cdSize);
  if (!readBytes(in, cdOffset, cd.data(), cd.size())) {
    return "The zip archive is truncated.";
  }

  bool haveStep = false;
  bool haveAny = false;
  std::size_t pos = 0;
  for (std::uint32_t i = 0; i < entryCount; ++i) {
    if (pos + 46 > cd.size() || le32(&cd[pos]) != 0x02014b50) {
      return "The zip archive is corrupt (bad directory entry).";
    }
    const unsigned char* p = &cd[pos];
    const std::size_t nameLen = le16(p + 28);
    const std::size_t extraLen = le16(p + 30);
    const std::size_t commentLen = le16(p + 32);
    if (pos + 46 + nameLen + extraLen + commentLen > cd.size()) {
      return "The zip archive is corrupt (bad directory entry).";
    }
    ZipEntry entry;
    entry.name.assign(reinterpret_cast<const char*>(p + 46), nameLen);
    entry.flags = le16(p + 8);
    entry.method = le16(p + 10);
    entry.crc = le32(p + 16);
    entry.compressedSize = le32(p + 20);
    entry.size = le32(p + 24);
    entry.localOffset = le32(p + 42);
    pos += 46 + nameLen + extraLen + commentLen;

    if (entry.name.empty() || entry.name.back() == '/' || entry.name.back() == '\\') {
      continue; // directory
    }
    const bool isStep = endsWithNoCase(entry.name, ".stp") || endsWithNoCase(entry.name, ".step") ||
                        endsWithNoCase(entry.name, ".p21");
    if (isStep && !haveStep) {
      chosen = entry;
      haveStep = true;
      haveAny = true;
    } else if (!haveAny) {
      chosen = entry;
      haveAny = true;
    }
  }
  if (!haveAny) {
    return "The zip archive contains no files.";
  }
  return {};
}

DecompressResult extractGzip(std::ifstream& in, std::uint64_t fileSize, Sink& sink) {
  DecompressResult result;
  in.clear();
  in.seekg(0);
  result.ok = inflateStream(in, fileSize, 15 + 32, true, sink);
  result.cancelled = sink.cancelled();
  if (!result.ok && !result.cancelled) {
    result.error = sink.error().empty() ? "The gzip data could not be decompressed." : sink.error();
  }
  return result;
}

DecompressResult extractZip(std::ifstream& in, std::uint64_t fileSize, Sink& sink) {
  DecompressResult result;
  ZipEntry entry;
  if (std::string error = findZipEntry(in, fileSize, entry); !error.empty()) {
    result.error = std::move(error);
    return result;
  }
  if ((entry.flags & 1u) != 0) {
    result.error = "The zip entry '" + entry.name + "' is encrypted.";
    return result;
  }
  if (entry.method != 0 && entry.method != 8) {
    result.error = "The zip entry '" + entry.name + "' uses an unsupported compression method.";
    return result;
  }
  if (entry.compressedSize == 0xFFFFFFFFu || entry.size == 0xFFFFFFFFu || entry.localOffset == 0xFFFFFFFFu) {
    result.error = "ZIP64 archives are not supported.";
    return result;
  }
  std::array<unsigned char, 30> local{};
  if (!readBytes(in, entry.localOffset, local.data(), local.size()) || le32(local.data()) != 0x04034b50) {
    result.error = "The zip archive is corrupt (bad local header).";
    return result;
  }
  // Sizes come from the central directory: the local header holds zeros when a data descriptor
  // follows the data.
  const std::uint64_t dataOffset = entry.localOffset + 30 + le16(&local[26]) + le16(&local[28]);
  if (dataOffset + entry.compressedSize > fileSize) {
    result.error = "The zip archive is truncated.";
    return result;
  }
  in.clear();
  in.seekg(static_cast<std::streamoff>(dataOffset));

  bool ok = false;
  if (entry.method == 8) {
    ok = inflateStream(in, entry.compressedSize, -15, false, sink);
  } else {
    std::vector<unsigned char> buf(kChunk);
    std::uint64_t remaining = entry.compressedSize;
    ok = true;
    while (remaining > 0) {
      const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buf.size()));
      in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(want));
      if (static_cast<std::size_t>(in.gcount()) != want) {
        sink.fail("The zip archive is truncated.");
        ok = false;
        break;
      }
      remaining -= want;
      if (!sink.write(buf.data(), want) ||
          !sink.poll(1.0 - static_cast<double>(remaining) / static_cast<double>(entry.compressedSize))) {
        ok = false;
        break;
      }
    }
  }
  result.cancelled = sink.cancelled();
  if (ok && (sink.total() != entry.size || sink.crc() != entry.crc)) {
    ok = false;
    sink.fail("The zip entry '" + entry.name + "' is corrupt (size or checksum mismatch).");
  }
  result.ok = ok;
  if (!ok && !result.cancelled) {
    result.error = sink.error().empty() ? "The zip entry could not be extracted." : sink.error();
  }
  return result;
}

} // namespace

Compression detectCompression(const std::string& utf8Path) {
  std::ifstream in(toPath(utf8Path), std::ios::binary);
  if (!in) {
    return Compression::None;
  }
  unsigned char magic[4] = {};
  in.read(reinterpret_cast<char*>(magic), 4);
  const std::streamsize got = in.gcount();
  if (got >= 2 && magic[0] == 0x1F && magic[1] == 0x8B) {
    return Compression::Gzip;
  }
  if (got == 4 && magic[0] == 'P' && magic[1] == 'K' &&
      ((magic[2] == 3 && magic[3] == 4) || (magic[2] == 5 && magic[3] == 6))) {
    return Compression::Zip;
  }
  return Compression::None;
}

DecompressResult decompressStepFile(const std::string& utf8InPath, Compression kind,
                                    const std::string& utf8OutPath, const DecompressOptions& options) {
  DecompressResult result;
  if (kind == Compression::None) {
    result.error = "The file is not compressed.";
    return result;
  }
  try {
    std::ifstream in(toPath(utf8InPath), std::ios::binary);
    if (!in) {
      result.error = "The file could not be opened.";
      return result;
    }
    in.seekg(0, std::ios::end);
    const std::uint64_t fileSize = static_cast<std::uint64_t>(in.tellg());
    in.seekg(0);

    std::ofstream out(toPath(utf8OutPath), std::ios::binary | std::ios::trunc);
    if (!out) {
      result.error = "Could not create a temporary file for the decompressed data.";
      return result;
    }
    Sink sink(out, options);
    result = kind == Compression::Gzip ? extractGzip(in, fileSize, sink) : extractZip(in, fileSize, sink);
    result.bytes = sink.total();
    out.flush();
    if (result.ok && !out) {
      result.ok = false;
      result.error = "Could not write the temporary decompressed file (disk full?).";
    }
  } catch (const std::exception& error) {
    result = {};
    result.error = std::string("Decompression failed: ") + error.what();
  }
  if (!result.ok) {
    std::error_code ec;
    fs::remove(toPath(utf8OutPath), ec);
  }
  return result;
}

} // namespace sc::core
