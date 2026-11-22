#include "app/UserViews.h"

#include "core/DocumentCache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

#include <cmath>

namespace sc::app {

namespace {

constexpr int FormatVersion = 1;

QJsonArray toArray(const gp_XYZ& xyz) {
  return QJsonArray{xyz.X(), xyz.Y(), xyz.Z()};
}

std::optional<gp_XYZ> xyzFrom(const QJsonValue& value) {
  const QJsonArray array = value.toArray();
  if (array.size() != 3) {
    return std::nullopt;
  }
  for (const QJsonValue& component : array) {
    if (!component.isDouble() || !std::isfinite(component.toDouble())) {
      return std::nullopt;
    }
  }
  return gp_XYZ(array[0].toDouble(), array[1].toDouble(), array[2].toDouble());
}

std::optional<gp_Dir> dirFrom(const QJsonValue& value) {
  const std::optional<gp_XYZ> xyz = xyzFrom(value);
  if (!xyz || xyz->Modulus() < 1e-12) {
    return std::nullopt;
  }
  return gp_Dir(*xyz);
}

bool writeJson(const QString& path, const QJsonObject& object, QString* error) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error != nullptr) {
      *error = file.errorString();
    }
    return false;
  }
  file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
  if (!file.commit()) {
    if (error != nullptr) {
      *error = file.errorString();
    }
    return false;
  }
  return true;
}

// Reads a JSON object; nothing when the file does not exist. 'error' is set when it exists but
// cannot be read.
std::optional<QJsonObject> readJson(const QString& path, QString& error) {
  QFile file(path);
  if (!file.exists()) {
    return std::nullopt;
  }
  if (!file.open(QIODevice::ReadOnly)) {
    error = file.errorString();
    return std::nullopt;
  }
  QJsonParseError parseError{};
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    error = parseError.error != QJsonParseError::NoError ? parseError.errorString()
                                                         : QStringLiteral("not a JSON object");
    return std::nullopt;
  }
  return document.object();
}

} // namespace

// --- UserView -------------------------------------------------------------------------

QJsonObject toJson(const UserView& view) {
  QJsonObject camera{
      {QStringLiteral("eye"), toArray(view.camera.eye.XYZ())},
      {QStringLiteral("center"), toArray(view.camera.center.XYZ())},
      {QStringLiteral("up"), toArray(view.camera.up.XYZ())},
      {QStringLiteral("projection"),
       view.camera.perspective ? QStringLiteral("perspective") : QStringLiteral("orthographic")},
      {QStringLiteral("scale"), view.camera.scale},
      {QStringLiteral("fieldOfView"), view.camera.fieldOfView},
  };
  QJsonArray sections;
  for (const render::SectionPlane& plane : view.sections) {
    sections.append(QJsonObject{
        {QStringLiteral("enabled"), plane.enabled},
        {QStringLiteral("normal"), toArray(plane.normal.XYZ())},
        {QStringLiteral("offset"), plane.offset},
        {QStringLiteral("capping"), plane.capping},
        {QStringLiteral("hatch"), plane.hatch},
    });
  }
  return QJsonObject{
      {QStringLiteral("name"), view.name},
      {QStringLiteral("camera"), camera},
      {QStringLiteral("hiddenParts"), QJsonArray::fromStringList(view.hiddenParts)},
      {QStringLiteral("sections"), sections},
  };
}

std::optional<UserView> userViewFromJson(const QJsonObject& object) {
  UserView view;
  view.name = object.value(QStringLiteral("name")).toString().trimmed();
  const QJsonObject camera = object.value(QStringLiteral("camera")).toObject();
  const std::optional<gp_XYZ> eye = xyzFrom(camera.value(QStringLiteral("eye")));
  const std::optional<gp_XYZ> center = xyzFrom(camera.value(QStringLiteral("center")));
  const std::optional<gp_Dir> up = dirFrom(camera.value(QStringLiteral("up")));
  if (view.name.isEmpty() || !eye || !center || !up || (*eye - *center).Modulus() < 1e-9) {
    return std::nullopt;
  }
  view.camera.eye = gp_Pnt(*eye);
  view.camera.center = gp_Pnt(*center);
  view.camera.up = *up;
  view.camera.perspective =
      camera.value(QStringLiteral("projection")).toString() == QLatin1String("perspective");
  view.camera.scale = camera.value(QStringLiteral("scale")).toDouble(view.camera.scale);
  view.camera.fieldOfView = camera.value(QStringLiteral("fieldOfView")).toDouble(view.camera.fieldOfView);
  for (const QJsonValue& id : object.value(QStringLiteral("hiddenParts")).toArray()) {
    if (id.isString() && !id.toString().isEmpty()) {
      view.hiddenParts.append(id.toString());
    }
  }
  const QJsonArray sections = object.value(QStringLiteral("sections")).toArray();
  for (int i = 0; i < static_cast<int>(view.sections.size()) && i < sections.size(); ++i) {
    const QJsonObject stored = sections[i].toObject();
    render::SectionPlane& plane = view.sections[static_cast<size_t>(i)];
    const std::optional<gp_Dir> normal = dirFrom(stored.value(QStringLiteral("normal")));
    if (!normal) {
      continue; // keep the default plane
    }
    plane.enabled = stored.value(QStringLiteral("enabled")).toBool();
    plane.normal = *normal;
    plane.offset = stored.value(QStringLiteral("offset")).toDouble();
    plane.capping = stored.value(QStringLiteral("capping")).toBool(true);
    plane.hatch = stored.value(QStringLiteral("hatch")).toBool(true);
  }
  return view;
}

QJsonArray toJson(const std::vector<UserView>& views) {
  QJsonArray array;
  for (const UserView& view : views) {
    array.append(toJson(view));
  }
  return array;
}

std::vector<UserView> userViewsFromJson(const QJsonArray& array) {
  std::vector<UserView> views;
  for (const QJsonValue& value : array) {
    if (std::optional<UserView> view = userViewFromJson(value.toObject())) {
      views.push_back(std::move(*view));
    }
  }
  return views;
}

// --- ModelDataStore -------------------------------------------------------------------

ModelDataStore::ModelDataStore(QString directory) : _directory(std::move(directory)) {}

QString ModelDataStore::defaultDirectory() {
  const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return root.isEmpty() ? QString() : root + QStringLiteral("/models");
}

QString ModelDataStore::documentPath(const QString& fingerprint) const {
  return _directory + QLatin1Char('/') + fingerprint + QStringLiteral(".json");
}

QString ModelDataStore::indexPath() const {
  return _directory + QStringLiteral("/index.json");
}

QHash<QString, QString> ModelDataStore::readIndex() const {
  QHash<QString, QString> index;
  QString error;
  const std::optional<QJsonObject> stored = readJson(indexPath(), error);
  if (stored) {
    const QJsonObject paths = stored->value(QStringLiteral("paths")).toObject();
    for (auto it = paths.begin(); it != paths.end(); ++it) {
      index.insert(it.key(), it.value().toString());
    }
  }
  return index; // an unreadable index only loses the edited-in-place fallback
}

bool ModelDataStore::writeIndex(const QHash<QString, QString>& index, QString* error) const {
  QJsonObject paths;
  for (auto it = index.begin(); it != index.end(); ++it) {
    paths.insert(it.key(), it.value());
  }
  return writeJson(indexPath(),
                   QJsonObject{{QStringLiteral("format"), FormatVersion}, {QStringLiteral("paths"), paths}},
                   error);
}

QString ModelDataStore::fingerprintOf(const QString& modelPath) {
  const QString key = QFileInfo(modelPath).absoluteFilePath();
  auto it = _fingerprints.find(key);
  if (it == _fingerprints.end()) {
    it = _fingerprints.insert(key, QString::fromStdString(core::fileFingerprint(key.toStdString())));
  }
  return it.value();
}

ModelDataStore::Loaded ModelDataStore::load(const QString& modelPath) {
  Loaded loaded;
  const QString fingerprint = fingerprintOf(modelPath);
  if (_directory.isEmpty() || fingerprint.isEmpty()) {
    return loaded;
  }
  QString error;
  if (std::optional<QJsonObject> data = readJson(documentPath(fingerprint), error)) {
    loaded.data = *data;
    return loaded;
  }
  if (!error.isEmpty()) {
    loaded.ok = false;
    loaded.error = error;
    return loaded;
  }
  // Nothing under this content: the file may have been edited in place.
  const QString previous = readIndex().value(QFileInfo(modelPath).absoluteFilePath());
  if (!previous.isEmpty() && previous != fingerprint) {
    if (std::optional<QJsonObject> data = readJson(documentPath(previous), error)) {
      loaded.data = *data;
      if (save(modelPath, loaded.data)) {
        QFile::remove(documentPath(previous)); // moved to the new content's fingerprint
      }
    }
  }
  return loaded;
}

bool ModelDataStore::save(const QString& modelPath, const QJsonObject& data, QString* error) {
  const QString fingerprint = fingerprintOf(modelPath);
  if (_directory.isEmpty() || fingerprint.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("the model file cannot be read");
    }
    return false;
  }
  const QString path = documentPath(fingerprint);
  QString readError;
  if (QFile::exists(path) && !readJson(path, readError) && !readError.isEmpty()) {
    // Keep what could not be read rather than overwrite it.
    QFile::remove(path + QStringLiteral(".unreadable"));
    QFile::rename(path, path + QStringLiteral(".unreadable"));
  }
  QJsonObject document = data;
  document.insert(QStringLiteral("format"), FormatVersion);
  document.insert(QStringLiteral("file"), QFileInfo(modelPath).fileName());
  if (!writeJson(path, document, error)) {
    return false;
  }
  QHash<QString, QString> index = readIndex();
  const QString key = QFileInfo(modelPath).absoluteFilePath();
  if (index.value(key) != fingerprint) {
    index.insert(key, fingerprint);
    writeIndex(index, nullptr); // only the edited-in-place fallback depends on it
  }
  return true;
}

} // namespace sc::app
