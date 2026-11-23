#include "core/FileFormat.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace sc::core {

namespace {

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

bool endsWith(const std::string& text, const std::string& suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// An IGES record: 80 columns, a section letter in column 73 and a sequence number after it.
bool isIgesRecord(const std::string& line) {
  if (line.size() < 80) {
    return false;
  }
  const char section = line[72];
  if (section != 'S' && section != 'G' && section != 'F' && section != 'C') {
    return false;
  }
  return std::all_of(line.begin() + 73, line.begin() + 80,
                     [](char c) { return c == ' ' || std::isdigit(static_cast<unsigned char>(c)) != 0; });
}

} // namespace

const char* displayName(FileFormat format) {
  switch (format) {
  case FileFormat::Step:
    return "STEP";
  case FileFormat::Iges:
    return "IGES";
  case FileFormat::Stl:
    return "STL";
  case FileFormat::Obj:
    return "OBJ";
  case FileFormat::Gltf:
    return "glTF";
  case FileFormat::Unknown:
    break;
  }
  return "Unknown";
}

bool isMeshFormat(FileFormat format) {
  return format == FileFormat::Stl || format == FileFormat::Obj || format == FileFormat::Gltf;
}

FileFormat formatFromExtension(const std::string& utf8Path) {
  std::string name = lower(std::filesystem::u8path(utf8Path).filename().u8string());
  if (endsWith(name, ".gz")) {
    name.resize(name.size() - 3);
  }
  for (const char* suffix : {".step", ".stp", ".p21", ".stpz"}) {
    if (endsWith(name, suffix)) {
      return FileFormat::Step;
    }
  }
  for (const char* suffix : {".igs", ".iges"}) {
    if (endsWith(name, suffix)) {
      return FileFormat::Iges;
    }
  }
  if (endsWith(name, ".stl")) {
    return FileFormat::Stl;
  }
  if (endsWith(name, ".obj")) {
    return FileFormat::Obj;
  }
  if (endsWith(name, ".gltf") || endsWith(name, ".glb")) {
    return FileFormat::Gltf;
  }
  return FileFormat::Unknown;
}

FileFormat detectFileFormat(const std::string& utf8Path, const std::string& nameForExtension) {
  std::ifstream in(std::filesystem::u8path(utf8Path), std::ios::binary);
  std::string head(4096, '\0');
  in.read(head.data(), static_cast<std::streamsize>(head.size()));
  head.resize(static_cast<size_t>(std::max<std::streamsize>(in.gcount(), 0)));

  size_t start = 0;
  if (head.compare(0, 3, "\xEF\xBB\xBF") == 0) {
    start = 3; // UTF-8 byte-order mark
  }
  while (start < head.size() && std::isspace(static_cast<unsigned char>(head[start])) != 0) {
    ++start;
  }
  if (head.compare(start, 12, "ISO-10303-21") == 0) {
    return FileFormat::Step;
  }
  // IGES: the first record (lines end in \n or \r\n; some writers omit the start section).
  const size_t end = head.find('\n');
  std::string first = head.substr(0, end == std::string::npos ? head.size() : end);
  if (!first.empty() && first.back() == '\r') {
    first.pop_back();
  }
  if (isIgesRecord(first)) {
    return FileFormat::Iges;
  }
  if (head.compare(0, 4, "glTF") == 0) {
    return FileFormat::Gltf; // GLB container
  }
  if (head.compare(start, 1, "{") == 0 && head.find("\"asset\"") != std::string::npos) {
    return FileFormat::Gltf; // glTF JSON
  }
  // Binary STL: an 80-byte header, a triangle count, then 50 bytes per triangle. Checked before
  // ASCII: binary headers often start with "solid" too.
  std::error_code ec;
  const auto size = std::filesystem::file_size(std::filesystem::u8path(utf8Path), ec);
  if (!ec && size >= 84 && head.size() >= 84) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(head.data() + 80);
    const std::uintmax_t count = std::uintmax_t(bytes[0]) | std::uintmax_t(bytes[1]) << 8 |
                                 std::uintmax_t(bytes[2]) << 16 | std::uintmax_t(bytes[3]) << 24;
    if (count > 0 && 84 + 50 * count == size) {
      return FileFormat::Stl;
    }
  }
  if (head.compare(start, 5, "solid") == 0 && head.find("facet") != std::string::npos) {
    return FileFormat::Stl; // ASCII STL
  }
  if (head.find("\nv ") != std::string::npos || head.compare(start, 2, "v ") == 0) {
    // OBJ: vertex records. Also checked by name below, so a short header misses nothing.
    if (head.find("\nf ") != std::string::npos || formatFromExtension(utf8Path) == FileFormat::Obj ||
        formatFromExtension(nameForExtension) == FileFormat::Obj) {
      return FileFormat::Obj;
    }
  }
  return formatFromExtension(nameForExtension.empty() ? utf8Path : nameForExtension);
}

std::optional<LengthUnit> lengthUnitFromIgesFlag(int flag) {
  switch (flag) {
  case 1:
    return LengthUnit::Inch;
  case 2:
    return LengthUnit::Millimeter;
  case 4:
    return LengthUnit::Foot;
  case 6:
    return LengthUnit::Meter;
  case 9:
    return LengthUnit::Micrometer;
  case 10:
    return LengthUnit::Centimeter;
  default:
    return std::nullopt; // 3: named in parameter 15; 5 miles, 7 km, 8 mils, 11 microinches
  }
}

} // namespace sc::core
