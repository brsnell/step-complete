#include "render/PmiFont.h"

#include "core/OccHandle.h"

#include <Font_FTFont.hxx>
#include <Font_FontAspect.hxx>
#include <Font_FontMgr.hxx>
#include <Font_SystemFont.hxx>
#include <Font_TextFormatter.hxx>
#include <NCollection_String.hxx>
#include <TCollection_AsciiString.hxx>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

#include <cmath>
#include <memory>
#include <mutex>
#include <string>

namespace sc::render {

namespace {

constexpr const char* FontResource = ":/fonts/STIXTwoMath-Regular.ttf";
constexpr const char* FallbackFamily = "Courier"; // OCCT's default font alias

struct FontState {
  bool registered = false;
  std::string family = FallbackFamily;
  std::unique_ptr<QTemporaryDir> tempDir; // lives for the process when the cache is not writable
};

FontState& state() {
  static FontState fontState;
  return fontState;
}

// Writes the resource into `dir` unless a file of the same size is already there.
// Returns the file path, or an empty string.
QString extractTo(const QString& dir) {
  if (dir.isEmpty() || !QDir().mkpath(dir)) {
    return {};
  }
  QFile resource(QString::fromLatin1(FontResource));
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
  FontState& fontState = state();
  try {
    QString path =
        extractTo(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/fonts"));
    if (path.isEmpty() || !registerFile(path)) {
      fontState.tempDir = std::make_unique<QTemporaryDir>();
      path = fontState.tempDir->isValid() ? extractTo(fontState.tempDir->path()) : QString();
      if (path.isEmpty() || !registerFile(path)) {
        fontState.family = FallbackFamily;
        return;
      }
    }
    fontState.registered = true;
  } catch (...) {
    fontState.registered = false;
    fontState.family = FallbackFamily;
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

double pmiTextWidth(const std::string& text, double fontSize) {
  if (text.empty()) {
    return 0.0;
  }
  const auto size = static_cast<unsigned int>(std::lround(fontSize));
  try {
    const OccHandle<Font_FTFont> font =
        Font_FTFont::FindAndCreate(pmiFontName(), Font_FontAspect_Regular, Font_FTFontParams(size, 72));
    if (!font.IsNull()) {
      OccHandle<Font_TextFormatter> formatter = new Font_TextFormatter();
      formatter->SetupAlignment(Graphic3d_HTA_LEFT, Graphic3d_VTA_TOP);
      formatter->Append(NCollection_String(text.c_str()), *font);
      formatter->Format();
      return static_cast<double>(formatter->ResultWidth());
    }
  } catch (...) {
    // fall through to the estimate
  }
  return static_cast<double>(text.size()) * fontSize * 0.6;
}

} // namespace sc::render
