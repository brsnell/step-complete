#include "render/PmiFont.h"

#include "core/OccHandle.h"

#include <Font_FontMgr.hxx>
#include <Font_SystemFont.hxx>
#include <TCollection_AsciiString.hxx>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

#include <memory>
#include <mutex>
#include <string>

namespace sc::render {

namespace {

constexpr const char* kFontResource = ":/fonts/STIXTwoMath-Regular.ttf";
constexpr const char* kFallbackFamily = "Courier"; // OCCT's default font alias

struct FontState {
  bool registered = false;
  std::string family = kFallbackFamily;
  std::unique_ptr<QTemporaryDir> tempDir; // lives for the process when the cache is not writable
};

FontState& state() {
  static FontState s;
  return s;
}

// Writes the resource into `dir` unless a file of the same size is already there.
// Returns the file path, or an empty string.
QString extractTo(const QString& dir) {
  if (dir.isEmpty() || !QDir().mkpath(dir)) {
    return {};
  }
  QFile resource(QString::fromLatin1(kFontResource));
  if (!resource.open(QIODevice::ReadOnly)) {
    return {};
  }
  const QString path = QDir(dir).filePath(QStringLiteral("STIXTwoMath-Regular.ttf"));
  if (QFileInfo::exists(path) && QFileInfo(path).size() == resource.size()) {
    return path;
  }
  QSaveFile out(path);
  if (!out.open(QIODevice::WriteOnly)) {
    return {};
  }
  const QByteArray data = resource.readAll();
  if (data.isEmpty() || out.write(data) != data.size() || !out.commit()) {
    return {};
  }
  return path;
}

bool registerFile(const QString& path) {
  const OccHandle<Font_SystemFont> font = Font_FontMgr::GetInstance()->CheckFont(path.toUtf8().constData());
  if (font.IsNull()) {
    return false;
  }
  Font_FontMgr::GetInstance()->RegisterFont(font, true);
  const TCollection_AsciiString& name = font->FontName();
  if (name.IsEmpty()) {
    return false;
  }
  state().family = name.ToCString();
  return true;
}

void initialize() {
  FontState& s = state();
  try {
    QString path =
        extractTo(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/fonts"));
    if (path.isEmpty() || !registerFile(path)) {
      s.tempDir = std::make_unique<QTemporaryDir>();
      path = s.tempDir->isValid() ? extractTo(s.tempDir->path()) : QString();
      if (path.isEmpty() || !registerFile(path)) {
        s.family = kFallbackFamily;
        return;
      }
    }
    s.registered = true;
  } catch (...) {
    s.registered = false;
    s.family = kFallbackFamily;
  }
}

} // namespace

bool ensurePmiFont() {
  static std::once_flag once;
  std::call_once(once, initialize);
  return state().registered;
}

const char* pmiFontName() {
  ensurePmiFont();
  return state().family.c_str();
}

} // namespace sc::render
