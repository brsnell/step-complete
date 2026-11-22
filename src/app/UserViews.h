#pragma once

#include "render/OcctViewport.h"
#include "render/SectionTool.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>
#include <vector>

namespace sc::app {

// A view the user saved (View > Saved Views; docs/designs/user-saved-views.md): the camera, the
// parts that were hidden and the section planes.
struct UserView {
  QString name;
  render::CameraState camera;
  QStringList hiddenParts; // core::AssemblyIndex::pathId of each hidden part
  std::array<render::SectionPlane, render::SectionTool::MaxPlanes> sections{};
};

QJsonObject toJson(const UserView& view);
// Nothing for an object without a name or a usable camera.
std::optional<UserView> userViewFromJson(const QJsonObject& object);
QJsonArray toJson(const std::vector<UserView>& views);
// Skips entries that cannot be read.
std::vector<UserView> userViewsFromJson(const QJsonArray& array);

// Data the user keeps about a model, one JSON document per model file in a folder (by default
// the app data folder's "models"). Each feature has its own key in the document ("views" now;
// material densities and markup later), so they never overwrite each other's data.
//
// The document is found by core::fileFingerprint (the file's content), so copies, moves and
// renames keep it. An index maps each path to the fingerprint last seen there: a file edited in
// place keeps its data, which moves to the new fingerprint.
class ModelDataStore {
public:
  explicit ModelDataStore(QString directory);
  static QString defaultDirectory();
  const QString& directory() const { return _directory; }

  struct Loaded {
    QJsonObject data; // empty when the model has none yet
    bool ok = true;   // false: a document exists but cannot be read (error says why)
    QString error;
  };
  Loaded load(const QString& modelPath);
  // Writes the whole document (atomically). A document that could not be read is kept beside it
  // as "<name>.unreadable" rather than lost. Returns false with a reason on failure.
  bool save(const QString& modelPath, const QJsonObject& data, QString* error = nullptr);

private:
  QString documentPath(const QString& fingerprint) const;
  QString indexPath() const;
  QHash<QString, QString> readIndex() const;
  bool writeIndex(const QHash<QString, QString>& index, QString* error) const;
  QString fingerprintOf(const QString& modelPath);

  QString _directory;
  QHash<QString, QString> _fingerprints; // per path, computed once per session
};

} // namespace sc::app
