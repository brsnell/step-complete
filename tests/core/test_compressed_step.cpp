#include "TestModels.h"

#include "core/CompressedStep.h"
#include "core/StepImporter.h"

#include <zlib.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace sc;

namespace {

std::string readAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeAll(const std::string& path, const std::string& data) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

// windowBits 15 + 16 makes a gzip stream, -15 a raw deflate stream (zip).
std::string deflateBytes(const std::string& data, int windowBits) {
  z_stream zs{};
  EXPECT_EQ(deflateInit2(&zs, 6, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY), Z_OK);
  std::string out(deflateBound(&zs, static_cast<uLong>(data.size())) + 64, '\0');
  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
  zs.avail_in = static_cast<uInt>(data.size());
  zs.next_out = reinterpret_cast<Bytef*>(out.data());
  zs.avail_out = static_cast<uInt>(out.size());
  EXPECT_EQ(deflate(&zs, Z_FINISH), Z_STREAM_END);
  out.resize(zs.total_out);
  deflateEnd(&zs);
  return out;
}

std::string gzipBytes(const std::string& data) {
  return deflateBytes(data, 15 + 16);
}

void put16(std::string& s, std::uint32_t v) {
  s.push_back(static_cast<char>(v & 0xFF));
  s.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void put32(std::string& s, std::uint32_t v) {
  put16(s, v & 0xFFFF);
  put16(s, v >> 16);
}

struct ZipFile {
  std::string name;
  std::string content;
  bool deflate = true;
  bool dataDescriptor = false;
};

std::string makeZip(const std::vector<ZipFile>& files) {
  std::string out;
  std::string central;
  for (const ZipFile& f : files) {
    const std::string data = f.deflate ? deflateBytes(f.content, -15) : f.content;
    const std::uint32_t crc = static_cast<std::uint32_t>(
        crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef*>(f.content.data()),
              static_cast<uInt>(f.content.size())));
    const std::uint32_t flags = f.dataDescriptor ? 8 : 0;
    const std::uint32_t method = f.deflate ? 8 : 0;
    const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
    put32(out, 0x04034b50);
    put16(out, 20);
    put16(out, flags);
    put16(out, method);
    put32(out, 0); // time/date
    put32(out, f.dataDescriptor ? 0 : crc);
    put32(out, f.dataDescriptor ? 0 : static_cast<std::uint32_t>(data.size()));
    put32(out, f.dataDescriptor ? 0 : static_cast<std::uint32_t>(f.content.size()));
    put16(out, static_cast<std::uint32_t>(f.name.size()));
    put16(out, 0);
    out += f.name;
    out += data;
    if (f.dataDescriptor) {
      put32(out, 0x08074b50);
      put32(out, crc);
      put32(out, static_cast<std::uint32_t>(data.size()));
      put32(out, static_cast<std::uint32_t>(f.content.size()));
    }
    put32(central, 0x02014b50);
    put16(central, 20);
    put16(central, 20);
    put16(central, flags);
    put16(central, method);
    put32(central, 0);
    put32(central, crc);
    put32(central, static_cast<std::uint32_t>(data.size()));
    put32(central, static_cast<std::uint32_t>(f.content.size()));
    put16(central, static_cast<std::uint32_t>(f.name.size()));
    put16(central, 0);
    put16(central, 0);
    put16(central, 0);
    put16(central, 0);
    put32(central, 0);
    put32(central, offset);
    central += f.name;
  }
  const std::uint32_t cdOffset = static_cast<std::uint32_t>(out.size());
  out += central;
  put32(out, 0x06054b50);
  put16(out, 0);
  put16(out, 0);
  put16(out, static_cast<std::uint32_t>(files.size()));
  put16(out, static_cast<std::uint32_t>(files.size()));
  put32(out, static_cast<std::uint32_t>(central.size()));
  put32(out, cdOffset);
  put16(out, 0);
  return out;
}

class CompressedStepTest : public ::testing::Test {
protected:
  void SetUp() override {
    plainPath = test::tempPath("compressed_base.stp");
    ASSERT_TRUE(test::writeStep(test::buildFixtureAssembly(false), plainPath));
    plain = readAll(plainPath);
    ASSERT_FALSE(plain.empty());
    reference = core::importStep(plainPath);
    ASSERT_TRUE(reference.ok) << reference.error;
  }

  void expectSameAsReference(const std::string& path) {
    const core::ImportResult result = core::importStep(path);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.stats.rootCount, reference.stats.rootCount);
    EXPECT_EQ(result.stats.uniqueParts, reference.stats.uniqueParts);
    EXPECT_EQ(result.document->sourcePath(), path);
  }

  void expectFailure(const std::string& path) {
    const core::ImportResult result = core::importStep(path);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.cancelled);
    EXPECT_FALSE(result.error.empty());
  }

  std::string plainPath;
  std::string plain;
  core::ImportResult reference;
};

} // namespace

TEST_F(CompressedStepTest, DetectsByMagicBytes) {
  const std::string path = test::tempPath("detect.dat");
  writeAll(path, gzipBytes(plain));
  EXPECT_EQ(core::detectCompression(path), core::Compression::Gzip);
  writeAll(path, makeZip({{"a.stp", plain, true, false}}));
  EXPECT_EQ(core::detectCompression(path), core::Compression::Zip);
  EXPECT_EQ(core::detectCompression(plainPath), core::Compression::None);
  EXPECT_EQ(core::detectCompression(test::tempPath("does_not_exist.stp")), core::Compression::None);
}

TEST_F(CompressedStepTest, ImportsGzip) {
  const std::string path = test::tempPath("model.stpz");
  writeAll(path, gzipBytes(plain));
  expectSameAsReference(path);
}

TEST_F(CompressedStepTest, ImportsMisnamedGzipAndConcatenatedMembers) {
  const std::string path = test::tempPath("misnamed.stp");
  const std::size_t half = plain.size() / 2;
  writeAll(path, gzipBytes(plain.substr(0, half)) + gzipBytes(plain.substr(half)));
  expectSameAsReference(path);
}

TEST_F(CompressedStepTest, ImportsZipVariants) {
  const std::string path = test::tempPath("model.zip");
  writeAll(path, makeZip({{"readme.txt", "hello", false, false},
                          {"dir/", "", false, false},
                          {"part.STEP", plain, true, false}}));
  expectSameAsReference(path);
  writeAll(path, makeZip({{"part.stp", plain, false, false}}));
  expectSameAsReference(path);
  writeAll(path, makeZip({{"part.stp", plain, true, true}}));
  expectSameAsReference(path);
  writeAll(path, makeZip({{"model.bin", plain, true, false}})); // no STEP name: first file entry
  expectSameAsReference(path);
}

TEST_F(CompressedStepTest, PlainFileUnaffected) {
  expectSameAsReference(plainPath);
}

TEST_F(CompressedStepTest, CorruptGzipFailsCleanly) {
  const std::string path = test::tempPath("corrupt.stpz");
  std::string data = gzipBytes(plain);
  for (std::size_t i = data.size() / 3; i < data.size() / 3 + 40 && i < data.size(); ++i) {
    data[i] = static_cast<char>(data[i] ^ 0x5A);
  }
  writeAll(path, data);
  expectFailure(path);

  writeAll(path, gzipBytes(plain).substr(0, data.size() / 2)); // truncated
  expectFailure(path);
}

TEST_F(CompressedStepTest, BadZipsFailCleanly) {
  const std::string path = test::tempPath("bad.zip");
  writeAll(path, makeZip({}));
  expectFailure(path);

  writeAll(path, makeZip({{"only_dir/", "", false, false}}));
  expectFailure(path);

  writeAll(path, makeZip({{"a.stp", plain, true, false}}).substr(0, 100)); // truncated archive
  expectFailure(path);

  // Corrupt entry data fails the checksum.
  std::string zip = makeZip({{"a.stp", plain, false, false}});
  zip[60] = static_cast<char>(zip[60] ^ 0xFF);
  writeAll(path, zip);
  expectFailure(path);
}

TEST_F(CompressedStepTest, SizeCapRefusesLargeOutput) {
  const std::string in = test::tempPath("cap.stpz");
  const std::string out = test::tempPath("cap.out");
  writeAll(in, gzipBytes(plain));
  core::DecompressOptions options;
  options.maxBytes = plain.size() / 2;
  const core::DecompressResult result = core::decompressStepFile(in, core::Compression::Gzip, out, options);
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.cancelled);
  EXPECT_NE(result.error.find("refusing"), std::string::npos);
  EXPECT_TRUE(readAll(out).empty()); // partial output removed
}

TEST_F(CompressedStepTest, CancelDuringDecompression) {
  const std::string path = test::tempPath("cancel.stpz");
  writeAll(path, gzipBytes(plain));
  core::CancelToken cancel;
  cancel.cancel();
  const core::ImportResult result = core::importStep(path, {}, {}, &cancel);
  EXPECT_FALSE(result.ok);
  EXPECT_TRUE(result.cancelled);
}

TEST_F(CompressedStepTest, ReportsDecompressingStage) {
  const std::string path = test::tempPath("stage.stpz");
  writeAll(path, gzipBytes(plain));
  bool sawStage = false;
  const core::ImportResult result = core::importStep(
      path, {}, [&](double, std::string_view stage) { sawStage = sawStage || stage == "Decompressing"; });
  EXPECT_TRUE(result.ok) << result.error;
  EXPECT_TRUE(sawStage);
}
