// Open-file cache: a model loaded from the cache must be indistinguishable from a fresh
// import. The real-model tests need tests/data/fetch.sh and SC_TEST_DATA_DIR; they skip otherwise.

#include "TestModels.h"
#include "core/AssemblyIndex.h"
#include "core/DocumentCache.h"
#include "core/PmiModel.h"
#include "core/StepImporter.h"

#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace sc;
namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

double since(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

std::string modelPath(const std::string& fileName) {
  const char* dir = std::getenv("SC_TEST_DATA_DIR");
  if (dir == nullptr) {
    return {};
  }
  const auto path = fs::path(dir) / fileName;
  return fs::exists(path) ? path.string() : std::string();
}

fs::path freshDir(const std::string& name) {
  const fs::path dir = fs::path(test::tempPath(name));
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir;
}

int countColors(const core::AssemblyIndex& index) {
  auto colors = XCAFDoc_DocumentTool::ColorTool(index.document()->ocaf()->Main());
  int colored = 0;
  for (const core::NodeId id : index.partInstances()) {
    Quantity_ColorRGBA color;
    const auto& node = index.node(id);
    if (node.inheritedColor || colors->GetColor(node.prototypeLabel, XCAFDoc_ColorSurf, color) ||
        colors->GetColor(node.prototypeLabel, XCAFDoc_ColorGen, color)) {
      ++colored;
    }
  }
  return colored;
}

void expectSameCamera(const core::StepCamera& a, const core::StepCamera& b) {
  EXPECT_EQ(a.name, b.name);
  EXPECT_TRUE(a.location.IsEqual(b.location, 0.0));
  // gp_Dir renormalizes on construction, so directions may differ by an ulp.
  EXPECT_TRUE(a.axis.IsEqual(b.axis, 1e-12));
  EXPECT_TRUE(a.refDirection.IsEqual(b.refDirection, 1e-12));
  EXPECT_EQ(a.perspective, b.perspective);
  EXPECT_TRUE(a.projectionPoint.IsEqual(b.projectionPoint, 0.0));
  EXPECT_EQ(a.viewPlaneDistance, b.viewPlaneDistance);
  EXPECT_EQ(a.frontPlaneDistance, b.frontPlaneDistance);
  EXPECT_EQ(a.backPlaneDistance, b.backPlaneDistance);
  EXPECT_EQ(a.frontPlaneClipping, b.frontPlaneClipping);
  EXPECT_EQ(a.backPlaneClipping, b.backPlaneClipping);
  EXPECT_EQ(a.sidesClipping, b.sidesClipping);
  EXPECT_EQ(a.windowWidth, b.windowWidth);
  EXPECT_EQ(a.windowHeight, b.windowHeight);
  EXPECT_EQ(a.windowCenterX, b.windowCenterX);
  EXPECT_EQ(a.windowCenterY, b.windowCenterY);
}

// Counts faces without a triangulation: the viewer would have to mesh them.
int countUntriangulatedFaces(const core::AssemblyIndex& index) {
  int missing = 0;
  for (const core::NodeId id : index.partInstances()) {
    for (TopExp_Explorer faces(index.prototypeShape(id), TopAbs_FACE); faces.More(); faces.Next()) {
      TopLoc_Location location;
      missing += BRep_Tool::Triangulation(TopoDS::Face(faces.Current()), location).IsNull() ? 1 : 0;
    }
  }
  return missing;
}

void expectSameModel(const core::ImportResult& fresh, const core::ImportResult& cached) {
  ASSERT_TRUE(cached.ok) << cached.error;
  ASSERT_TRUE(cached.stats.fromCache);
  const core::AssemblyIndex a(fresh.document);
  const core::AssemblyIndex b(cached.document);
  ASSERT_EQ(a.nodes().size(), b.nodes().size());
  EXPECT_EQ(a.uniquePartCount(), b.uniquePartCount());
  for (size_t i = 0; i < a.nodes().size(); ++i) {
    EXPECT_EQ(a.nodes()[i].name, b.nodes()[i].name);
    EXPECT_EQ(a.nodes()[i].prototypeName, b.nodes()[i].prototypeName);
    EXPECT_EQ(a.nodes()[i].children, b.nodes()[i].children);
  }
  EXPECT_EQ(countColors(a), countColors(b));
  EXPECT_EQ(countUntriangulatedFaces(a), 0);
  EXPECT_EQ(countUntriangulatedFaces(b), 0) << "triangulation must come from the cache";

  const core::PmiModel pa(fresh.document);
  const core::PmiModel pb(cached.document);
  ASSERT_EQ(pa.items().size(), pb.items().size());
  for (size_t i = 0; i < pa.items().size(); ++i) {
    EXPECT_EQ(pa.items()[i].text, pb.items()[i].text);
    EXPECT_EQ(pa.items()[i].typeName, pb.items()[i].typeName);
    EXPECT_EQ(pa.items()[i].frame, pb.items()[i].frame);
  }
  EXPECT_EQ(pa.views().size(), pb.views().size());

  EXPECT_EQ(fresh.document->fileUnit(), cached.document->fileUnit());
  EXPECT_EQ(fresh.document->fileUnitName(), cached.document->fileUnitName());
  ASSERT_EQ(fresh.document->cameras().size(), cached.document->cameras().size());
  for (size_t i = 0; i < fresh.document->cameras().size(); ++i) {
    expectSameCamera(fresh.document->cameras()[i], cached.document->cameras()[i]);
  }

  ASSERT_EQ(fresh.messages.size() + 1, cached.messages.size());
  for (size_t i = 0; i < fresh.messages.size(); ++i) {
    EXPECT_EQ(fresh.messages[i].text, cached.messages[i].text);
    EXPECT_EQ(fresh.messages[i].count, cached.messages[i].count);
    EXPECT_EQ(fresh.messages[i].severity, cached.messages[i].severity);
  }
  EXPECT_EQ(cached.messages.back().text, "Loaded from cache");
  EXPECT_EQ(fresh.stats.rootCount, cached.stats.rootCount);
  EXPECT_EQ(fresh.stats.uniqueParts, cached.stats.uniqueParts);
}

void roundTrip(const std::string& file) {
  const std::string path = modelPath(file);
  if (path.empty()) {
    GTEST_SKIP() << file << " not available (run tests/data/fetch.sh and set SC_TEST_DATA_DIR)";
  }
  const fs::path dir = freshDir("cache_" + file);
  core::ImportOptions options;
  options.cacheDir = dir.string();
  options.deferCacheStore = true;

  Clock::time_point start = Clock::now();
  const core::ImportResult fresh = core::importStep(path, options);
  const double read = since(start);
  ASSERT_TRUE(fresh.ok) << fresh.error;
  EXPECT_FALSE(fresh.stats.fromCache);

  start = Clock::now();
  ASSERT_TRUE(core::storeInCache(path, fresh, options));
  const double store = since(start);

  start = Clock::now();
  const core::ImportResult cached = core::importStep(path, options);
  const double load = since(start);
  expectSameModel(fresh, cached);

  std::printf("[CACHE] %s read=%.3fs store=%.3fs load=%.3fs size=%.2fMB\n", file.c_str(), read, store, load,
              static_cast<double>(core::DocumentCache(dir).totalBytes()) / 1e6);
  fs::remove_all(dir);
}

} // namespace

TEST(DocumentCache, As1Ap214) {
  roundTrip("as1-oc-214.stp");
}
TEST(DocumentCache, As1Ap203) {
  roundTrip("as1_pe_203.stp");
}
TEST(DocumentCache, NistCtc01) {
  roundTrip("nist_ctc_01_asme1_ap242-e1.stp");
}
TEST(DocumentCache, NistCtc02) {
  roundTrip("nist_ctc_02_asme1_ap242-e2.stp");
}
TEST(DocumentCache, NistCtc03) {
  roundTrip("nist_ctc_03_asme1_ap242-e2.stp");
}
TEST(DocumentCache, NistCtc04) {
  roundTrip("nist_ctc_04_asme1_ap242-e1.stp");
}
TEST(DocumentCache, NistCtc05) {
  roundTrip("nist_ctc_05_asme1_ap242-e1.stp");
}
TEST(DocumentCache, NistFtc06) {
  roundTrip("nist_ftc_06_asme1_ap242-e2.stp");
}
TEST(DocumentCache, NistFtc07) {
  roundTrip("nist_ftc_07_asme1_ap242-e2.stp");
}
TEST(DocumentCache, NistFtc10) {
  roundTrip("nist_ftc_10_asme1_ap242-e2.stp");
}

TEST(DocumentCache, ImportStoresAutomaticallyAndLoadsFixtureWithInchUnit) {
  const std::string step = test::tempPath("cache_fixture.stp");
  ASSERT_TRUE(test::writeStep(test::buildFixtureAssembly(false), step, "INCH"));
  const fs::path dir = freshDir("cache_fixture_dir");
  core::ImportOptions options;
  options.cacheDir = dir.string();

  const core::ImportResult fresh = core::importStep(step, options);
  ASSERT_TRUE(fresh.ok) << fresh.error;
  EXPECT_FALSE(fresh.stats.fromCache);
  const core::ImportResult cached = core::importStep(step, options);
  expectSameModel(fresh, cached);
  EXPECT_EQ(cached.document->sourcePath(), step);

  // Without premesh the cache is not used at all.
  core::ImportOptions noMesh = options;
  noMesh.premesh = false;
  EXPECT_FALSE(core::importStep(step, noMesh).stats.fromCache);
  fs::remove_all(dir);
}

TEST(DocumentCache, CorruptEntryIsAMissAndIsDeleted) {
  const std::string step = test::tempPath("cache_corrupt.stp");
  ASSERT_TRUE(test::writeStep(test::buildFixtureAssembly(false), step));
  const fs::path dir = freshDir("cache_corrupt_dir");
  core::ImportOptions options;
  options.cacheDir = dir.string();
  ASSERT_TRUE(core::importStep(step, options).ok);

  const std::string key = core::cacheKey(step, "pmi=1");
  const core::DocumentCache cache(dir);
  ASSERT_TRUE(cache.contains(key));

  for (const char* victim : {".cbf", ".meta"}) {
    // Truncate the file to half its size.
    const fs::path file = dir / (key + victim);
    fs::resize_file(file, fs::file_size(file) / 2);
    EXPECT_FALSE(cache.load(key).has_value()) << victim;
    EXPECT_FALSE(cache.contains(key)) << victim;

    const core::ImportResult again = core::importStep(step, options); // re-creates the entry
    ASSERT_TRUE(again.ok);
    EXPECT_FALSE(again.stats.fromCache);
    ASSERT_TRUE(cache.contains(key));
  }

  // Garbage instead of a document.
  {
    std::ofstream out(dir / (key + ".cbf"), std::ios::binary | std::ios::trunc);
    out << "this is not a BinXCAF file";
  }
  const core::ImportResult recovered = core::importStep(step, options);
  ASSERT_TRUE(recovered.ok);
  EXPECT_FALSE(recovered.stats.fromCache);
  EXPECT_TRUE(core::importStep(step, options).stats.fromCache);
  fs::remove_all(dir);
}

TEST(DocumentCache, KeyChangesWithContentAndVariant) {
  const std::string step = test::tempPath("cache_key.stp");
  ASSERT_TRUE(test::writeStep(test::buildFixtureAssembly(false), step));
  const std::string key = core::cacheKey(step);
  ASSERT_FALSE(key.empty());
  EXPECT_EQ(key, core::cacheKey(step));
  EXPECT_NE(key, core::cacheKey(step, "pmi=0"));
  EXPECT_TRUE(core::cacheKey(test::tempPath("cache_missing.stp")).empty());

  // One byte flipped in the first block, same size, modification time restored:
  // the content hash must still notice.
  const auto mtime = fs::last_write_time(step);
  {
    std::fstream file(step, std::ios::in | std::ios::out | std::ios::binary);
    char c = 0;
    file.seekg(100);
    file.get(c);
    file.seekp(100);
    file.put(static_cast<char>(c ^ 0x01));
  }
  fs::last_write_time(step, mtime);
  EXPECT_NE(key, core::cacheKey(step));

  // Appending changes the size.
  const std::string key2 = core::cacheKey(step);
  {
    std::ofstream file(step, std::ios::binary | std::ios::app);
    file << "\n";
  }
  EXPECT_NE(key2, core::cacheKey(step));
}

TEST(DocumentCache, PruneRespectsMaxBytesAndDropsLeastRecentlyUsed) {
  const fs::path dir = freshDir("cache_prune_dir");
  const auto now = fs::file_time_type::clock::now();
  const auto write = [&](const std::string& key, size_t bytes, int ageMinutes) {
    std::ofstream(dir / (key + ".cbf"), std::ios::binary) << std::string(bytes, 'x');
    std::ofstream(dir / (key + ".meta"), std::ios::binary) << "m";
    fs::last_write_time(dir / (key + ".cbf"), now - std::chrono::minutes(ageMinutes));
    fs::last_write_time(dir / (key + ".meta"), now - std::chrono::minutes(ageMinutes));
  };
  write("aaaa", 4000, 30);
  write("bbbb", 4000, 20);
  write("cccc", 4000, 10);

  const core::DocumentCache cache(dir, 9000);
  cache.prune();
  EXPECT_LE(cache.totalBytes(), 9000u);
  EXPECT_FALSE(cache.contains("aaaa"));
  EXPECT_TRUE(cache.contains("bbbb"));
  EXPECT_TRUE(cache.contains("cccc"));

  const core::DocumentCache tiny(dir, 1);
  tiny.prune();
  EXPECT_EQ(tiny.totalBytes(), 0u);
  fs::remove_all(dir);
}
