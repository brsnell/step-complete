#include "core/DocumentCache.h"

#include "core/OccCompat.h"
#include "core/OccHandle.h"
#include "core/Version.h"

#include <BRep_Tool.hxx>
#include <BinXCAFDrivers.hxx>
#include <BinXCAFDrivers_DocumentStorageDriver.hxx>
#include <Message.hxx>
#include <PCDM_ReaderStatus.hxx>
#include <Prs3d_Drawer.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>

namespace sc::core {

namespace fs = std::filesystem;

namespace {

// Bump when the entry layout or what is stored in it changes.
constexpr int kCacheFormatVersion = 1;
constexpr const char* kMetaMagic = "SCMETA";

// --- Hashing --------------------------------------------------------------------

class Fnv1a64 {
public:
  void add(const void* data, size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
      m_state = (m_state ^ bytes[i]) * 0x100000001b3ull;
    }
  }
  void add(std::string_view text) {
    addU64(text.size());
    add(text.data(), text.size());
  }
  void addU64(std::uint64_t value) { add(&value, sizeof value); }
  void addDouble(double value) { add(&value, sizeof value); }
  std::uint64_t value() const { return m_state; }

private:
  std::uint64_t m_state = 0xcbf29ce484222325ull;
};

constexpr std::uint64_t kBlock = std::uint64_t{1} << 20; // 1 MiB
constexpr int kSampleBlocks = 6;
constexpr size_t kSampleSize = 64 * 1024;

fs::path pathFromUtf8(const std::string& utf8) {
#if defined(_WIN32)
  return fs::u8path(utf8);
#else
  return fs::path(utf8);
#endif
}

std::string toHex(std::uint64_t value) {
  char buffer[17];
  std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(value));
  return buffer;
}

// --- Meta (sidecar) file ----------------------------------------------------------
// Whitespace-separated tokens; strings are length-prefixed ("5:hello") and doubles are
// stored as the hex of their IEEE bit pattern, so they round-trip exactly.

std::uint64_t bitsOf(double value) {
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof bits);
  return bits;
}

double doubleOf(std::uint64_t bits) {
  double value = 0.0;
  std::memcpy(&value, &bits, sizeof value);
  return value;
}

class MetaWriter {
public:
  explicit MetaWriter(std::ostream& os) : m_os(os) {}
  void i(long long v) { m_os << v << '\n'; }
  void d(double v) { m_os << std::hex << bitsOf(v) << std::dec << '\n'; }
  void s(const std::string& v) {
    m_os << v.size() << ':';
    m_os.write(v.data(), static_cast<std::streamsize>(v.size()));
    m_os << '\n';
  }
  void word(const char* v) { m_os << v << '\n'; }
  void pnt(const gp_Pnt& p) {
    d(p.X());
    d(p.Y());
    d(p.Z());
  }
  void dir(const gp_Dir& v) {
    d(v.X());
    d(v.Y());
    d(v.Z());
  }
  void opt(const std::optional<double>& v) {
    i(v ? 1 : 0);
    d(v.value_or(0.0));
  }

private:
  std::ostream& m_os;
};

class MetaReader {
public:
  explicit MetaReader(std::istream& is) : m_is(is) {}
  bool ok() const { return !m_failed && !m_is.fail(); }
  long long i() {
    long long v = 0;
    m_is >> v;
    return v;
  }
  double d() {
    std::uint64_t bits = 0;
    m_is >> std::hex >> bits >> std::dec;
    return doubleOf(bits);
  }
  std::string s() {
    size_t size = 0;
    char colon = 0;
    m_is >> size >> colon;
    if (!m_is || colon != ':' || size > (std::size_t{1} << 28)) {
      m_failed = true;
      return {};
    }
    std::string out(size, '\0');
    m_is.read(out.data(), static_cast<std::streamsize>(size));
    return out;
  }
  std::string word() {
    std::string v;
    m_is >> v;
    return v;
  }
  gp_Pnt pnt() {
    const double x = d();
    const double y = d();
    const double z = d();
    return gp_Pnt(x, y, z);
  }
  // gp_Dir renormalizes, so a stored unit vector may differ from the original by an ulp.
  gp_Dir dir() {
    const double x = d();
    const double y = d();
    const double z = d();
    if (std::abs(x) + std::abs(y) + std::abs(z) < 1e-9) {
      m_failed = true;
      return gp_Dir();
    }
    return gp_Dir(x, y, z);
  }
  std::optional<double> opt() {
    const bool has = i() != 0;
    const double v = d();
    return has ? std::optional<double>(v) : std::nullopt;
  }

private:
  std::istream& m_is;
  bool m_failed = false;
};

void writeMeta(std::ostream& os, const Document& document, const std::vector<ImportMessage>& messages,
               int rootCount, int uniqueParts) {
  MetaWriter w(os);
  w.word(kMetaMagic);
  w.i(kCacheFormatVersion);
  w.s(document.sourcePath());
  w.s(document.fileUnitName());
  w.i(document.fileUnit() ? static_cast<int>(*document.fileUnit()) : -1);
  w.i(rootCount);
  w.i(uniqueParts);
  w.i(static_cast<long long>(document.cameras().size()));
  for (const StepCamera& c : document.cameras()) {
    w.s(c.name);
    w.pnt(c.location);
    w.dir(c.axis);
    w.dir(c.refDirection);
    w.i(c.perspective);
    w.pnt(c.projectionPoint);
    w.d(c.viewPlaneDistance);
    w.opt(c.frontPlaneDistance);
    w.opt(c.backPlaneDistance);
    w.i(c.frontPlaneClipping);
    w.i(c.backPlaneClipping);
    w.i(c.sidesClipping);
    w.d(c.windowWidth);
    w.d(c.windowHeight);
    w.d(c.windowCenterX);
    w.d(c.windowCenterY);
  }
  w.i(static_cast<long long>(messages.size()));
  for (const ImportMessage& m : messages) {
    w.i(static_cast<int>(m.severity));
    w.i(m.count);
    w.s(m.text);
  }
  w.word("END"); // detects truncated files
}

bool readMeta(std::istream& is, Document& document, std::vector<ImportMessage>& messages, int& rootCount,
              int& uniqueParts) {
  MetaReader r(is);
  if (r.word() != kMetaMagic || r.i() != kCacheFormatVersion) {
    return false;
  }
  document.setSourcePath(r.s());
  std::string unitName = r.s();
  const int unit = static_cast<int>(r.i());
  if (unit < -1 || unit > static_cast<int>(LengthUnit::Foot)) {
    return false;
  }
  document.setFileUnit(std::move(unitName),
                       unit >= 0 ? std::optional<LengthUnit>(static_cast<LengthUnit>(unit)) : std::nullopt);
  rootCount = static_cast<int>(r.i());
  uniqueParts = static_cast<int>(r.i());

  const long long cameraCount = r.i();
  if (!r.ok() || cameraCount < 0 || cameraCount > 1000000) {
    return false;
  }
  std::vector<StepCamera> cameras;
  for (long long n = 0; n < cameraCount && r.ok(); ++n) {
    StepCamera c;
    c.name = r.s();
    c.location = r.pnt();
    c.axis = r.dir();
    c.refDirection = r.dir();
    c.perspective = r.i() != 0;
    c.projectionPoint = r.pnt();
    c.viewPlaneDistance = r.d();
    c.frontPlaneDistance = r.opt();
    c.backPlaneDistance = r.opt();
    c.frontPlaneClipping = r.i() != 0;
    c.backPlaneClipping = r.i() != 0;
    c.sidesClipping = r.i() != 0;
    c.windowWidth = r.d();
    c.windowHeight = r.d();
    c.windowCenterX = r.d();
    c.windowCenterY = r.d();
    cameras.push_back(std::move(c));
  }
  const long long messageCount = r.i();
  if (!r.ok() || messageCount < 0 || messageCount > 10000000) {
    return false;
  }
  for (long long n = 0; n < messageCount && r.ok(); ++n) {
    ImportMessage m;
    const int severity = static_cast<int>(r.i());
    if (severity < 0 || severity > static_cast<int>(ImportMessage::Severity::Error)) {
      return false;
    }
    m.severity = static_cast<ImportMessage::Severity>(severity);
    m.count = static_cast<int>(r.i());
    m.text = r.s();
    messages.push_back(std::move(m));
  }
  if (!r.ok() || r.word() != "END") {
    return false;
  }
  document.setCameras(std::move(cameras));
  return true;
}

// --- BinXCAF ----------------------------------------------------------------------

OccHandle<XCAFApp_Application> xcafApplication() {
  static std::once_flag once;
  OccHandle<XCAFApp_Application> app = XCAFApp_Application::GetApplication();
  std::call_once(once, [&app] { BinXCAFDrivers::DefineFormat(app); });
  return app;
}

bool writeBinXCAF(const OccHandle<TDocStd_Document>& doc, const fs::path& file) {
  // The application's own SaveAs would write without triangulations; drive the
  // storage driver directly so they are included.
  OccHandle<BinXCAFDrivers_DocumentStorageDriver> driver = new BinXCAFDrivers_DocumentStorageDriver();
  driver->SetWithTriangles(Message::DefaultMessenger(), true);
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  driver->Write(doc, out);
  out.flush();
  return out.good();
}

OccHandle<TDocStd_Document> readBinXCAF(const fs::path& file) {
  OccHandle<XCAFApp_Application> app = xcafApplication();
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    return {};
  }
  OccHandle<TDocStd_Document> doc;
  const PCDM_ReaderStatus status = app->Open(in, doc);
  if (status != PCDM_RS_OK || doc.IsNull()) {
    if (!doc.IsNull() && doc->IsOpened()) {
      app->Close(doc);
    }
    return {};
  }
  return doc;
}

// An entry without any triangulation would silently force meshing on the UI thread. The
// importer only stores premeshed documents, so a mesh-less entry with faces is damaged.
bool hasTriangulatedFaces(const OccHandle<TDocStd_Document>& doc) {
  OccHandle<XCAFDoc_ShapeTool> tool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  occ::LabelSequence labels;
  tool->GetShapes(labels);
  bool anyFace = false;
  for (const TDF_Label& label : labels) {
    if (XCAFDoc_ShapeTool::IsAssembly(label)) {
      continue;
    }
    for (TopExp_Explorer faces(XCAFDoc_ShapeTool::GetShape(label), TopAbs_FACE); faces.More(); faces.Next()) {
      anyFace = true;
      TopLoc_Location location;
      if (!BRep_Tool::Triangulation(TopoDS::Face(faces.Current()), location).IsNull()) {
        return true;
      }
    }
  }
  return !anyFace;
}

} // namespace

std::string cacheKey(const std::string& utf8Path, std::string_view variant) {
  std::error_code ec;
  const fs::path path = pathFromUtf8(utf8Path);
  const std::uint64_t size = fs::file_size(path, ec);
  if (ec) {
    return {};
  }
  const auto mtime = fs::last_write_time(path, ec);
  if (ec) {
    return {};
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }

  Fnv1a64 hash;
  hash.addU64(kCacheFormatVersion);
  hash.add(kAppVersion);
  hash.add(OCC_VERSION_COMPLETE);
  hash.add(variant);
  {
    // Mesh parameters of the pre-mesher: a default drawer, like the viewer's.
    OccHandle<Prs3d_Drawer> drawer = new Prs3d_Drawer();
    hash.addDouble(drawer->DeviationCoefficient());
    hash.addDouble(drawer->DeviationAngle());
    hash.addDouble(drawer->MaximalChordialDeviation());
  }
  hash.addU64(size);
  hash.addU64(static_cast<std::uint64_t>(mtime.time_since_epoch().count()));

  std::vector<char> buffer;
  const auto hashRange = [&](std::uint64_t offset, size_t length) {
    length = static_cast<size_t>(std::min<std::uint64_t>(length, size > offset ? size - offset : 0));
    if (length == 0) {
      return;
    }
    buffer.resize(length);
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    in.read(buffer.data(), static_cast<std::streamsize>(length));
    const auto got = static_cast<size_t>(std::max<std::streamsize>(in.gcount(), 0));
    hash.addU64(offset);
    hash.add(buffer.data(), got);
  };
  hashRange(0, kBlock);
  if (size > 2 * kBlock) {
    hashRange(size - kBlock, kBlock);
    const std::uint64_t middle = size - 2 * kBlock;
    for (int n = 1; n <= kSampleBlocks; ++n) {
      hashRange(kBlock + middle * static_cast<std::uint64_t>(n) / (kSampleBlocks + 1), kSampleSize);
    }
  } else if (size > kBlock) {
    hashRange(kBlock, kBlock);
  }
  return toHex(hash.value());
}

DocumentCache::DocumentCache(fs::path dir, std::uint64_t maxBytes)
    : m_dir(std::move(dir)), m_maxBytes(maxBytes) {}

fs::path DocumentCache::entryPath(const std::string& key, const char* extension) const {
  return m_dir / (key + extension);
}

bool DocumentCache::contains(const std::string& key) const {
  std::error_code ec;
  return !key.empty() && fs::exists(entryPath(key, ".meta"), ec) && fs::exists(entryPath(key, ".cbf"), ec);
}

void DocumentCache::remove(const std::string& key) const {
  std::error_code ec;
  fs::remove(entryPath(key, ".meta"), ec);
  fs::remove(entryPath(key, ".cbf"), ec);
}

std::optional<CachedImport> DocumentCache::load(const std::string& key) const {
  if (!contains(key)) {
    return std::nullopt;
  }
  try {
    CachedImport out;
    auto document = std::make_shared<Document>(readBinXCAF(entryPath(key, ".cbf")));
    bool ok = !document->ocaf().IsNull();
    if (ok) {
      std::ifstream meta(entryPath(key, ".meta"), std::ios::binary);
      ok = meta && readMeta(meta, *document, out.messages, out.rootCount, out.uniqueParts) &&
           hasTriangulatedFaces(document->ocaf());
    }
    if (!ok) {
      document.reset();
      remove(key);
      return std::nullopt;
    }
    out.document = std::move(document);
    // Touch both files so prune() sees this entry as recently used.
    std::error_code ec;
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(entryPath(key, ".cbf"), now, ec);
    fs::last_write_time(entryPath(key, ".meta"), now, ec);
    return out;
  } catch (const Standard_Failure&) {
  } catch (const std::exception&) {
  }
  remove(key);
  return std::nullopt;
}

bool DocumentCache::store(const std::string& key, const Document& document,
                          const std::vector<ImportMessage>& messages, int rootCount, int uniqueParts) const {
  if (key.empty() || document.ocaf().IsNull()) {
    return false;
  }
  const fs::path cbf = entryPath(key, ".cbf");
  const fs::path meta = entryPath(key, ".meta");
  const fs::path cbfTmp = entryPath(key, ".cbf.tmp");
  const fs::path metaTmp = entryPath(key, ".meta.tmp");
  std::error_code ec;
  bool ok = false;
  try {
    fs::create_directories(m_dir, ec);
    ok = writeBinXCAF(document.ocaf(), cbfTmp);
    if (ok) {
      std::ofstream out(metaTmp, std::ios::binary | std::ios::trunc);
      writeMeta(out, document, messages, rootCount, uniqueParts);
      out.flush();
      ok = out.good();
    }
    if (ok) {
      // The meta file is renamed last: an entry only exists once both are in place.
      fs::rename(cbfTmp, cbf, ec);
      ok = !ec;
    }
    if (ok) {
      fs::rename(metaTmp, meta, ec);
      ok = !ec;
    }
  } catch (const Standard_Failure&) {
    ok = false;
  } catch (const std::exception&) {
    ok = false;
  }
  if (!ok) {
    fs::remove(cbfTmp, ec);
    fs::remove(metaTmp, ec);
    fs::remove(cbf, ec);
    return false;
  }
  prune();
  return true;
}

std::uint64_t DocumentCache::totalBytes() const {
  std::uint64_t total = 0;
  std::error_code ec;
  for (fs::directory_iterator it(m_dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code fileEc;
    if (it->is_regular_file(fileEc)) {
      total += it->file_size(fileEc);
    }
  }
  return total;
}

void DocumentCache::prune() const {
  struct Entry {
    std::uint64_t bytes = 0;
    fs::file_time_type mtime{};
  };
  std::map<std::string, Entry> entries; // by key
  std::error_code ec;
  for (fs::directory_iterator it(m_dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code fileEc;
    if (!it->is_regular_file(fileEc)) {
      continue;
    }
    const std::string name = it->path().filename().string();
    const size_t dot = name.find('.');
    if (dot == std::string::npos) {
      continue;
    }
    Entry& entry = entries[name.substr(0, dot)];
    entry.bytes += it->file_size(fileEc);
    entry.mtime = std::max(entry.mtime, it->last_write_time(fileEc));
  }
  std::vector<std::pair<std::string, Entry>> ordered(entries.begin(), entries.end());
  std::uint64_t total = 0;
  for (const auto& item : ordered) {
    total += item.second.bytes;
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const auto& a, const auto& b) { return a.second.mtime < b.second.mtime; });
  for (const auto& [key, entry] : ordered) {
    if (total <= m_maxBytes) {
      break;
    }
    remove(key);
    fs::remove(entryPath(key, ".cbf.tmp"), ec);
    fs::remove(entryPath(key, ".meta.tmp"), ec);
    total -= std::min(total, entry.bytes);
  }
}

} // namespace sc::core
