#include "app/MarkupJson.h"

#include <QJsonArray>
#include <QJsonValue>

#include <cmath>

namespace sc::app {

namespace {

const QString FormatTag = QStringLiteral("step-complete-markup");
constexpr int FormatVersion = 1;

QJsonArray toJson(const gp_Pnt& point) {
  return QJsonArray{point.X(), point.Y(), point.Z()};
}

bool isNumber(const QJsonValue& value) {
  return value.isDouble() && std::isfinite(value.toDouble());
}

std::optional<gp_Pnt> pointFrom(const QJsonArray& array, int first) {
  if (first < 0 || first + 2 >= array.size()) {
    return std::nullopt;
  }
  for (int i = first; i < first + 3; ++i) {
    if (!isNumber(array.at(i))) {
      return std::nullopt;
    }
  }
  return gp_Pnt(array.at(first).toDouble(), array.at(first + 1).toDouble(), array.at(first + 2).toDouble());
}

} // namespace

QJsonObject toJson(const core::Markup& markup) {
  QJsonArray notes;
  for (const core::MarkupNote& note : markup.notes()) {
    notes.append(QJsonObject{{QStringLiteral("text"), QString::fromStdString(note.text)},
                             {QStringLiteral("part"), QString::fromStdString(note.part)},
                             {QStringLiteral("point"), toJson(note.point)}});
  }
  QJsonArray strokes;
  for (const core::MarkupStroke& stroke : markup.strokes()) {
    QJsonArray points;
    for (const gp_Pnt& point : stroke.points) {
      points.append(point.X());
      points.append(point.Y());
      points.append(point.Z());
    }
    strokes.append(QJsonObject{{QStringLiteral("part"), QString::fromStdString(stroke.part)},
                               {QStringLiteral("points"), points}});
  }
  return QJsonObject{{QStringLiteral("notes"), notes}, {QStringLiteral("strokes"), strokes}};
}

core::Markup markupFromJson(const QJsonObject& object) {
  core::Markup markup;
  for (const QJsonValue& value : object.value(QStringLiteral("notes")).toArray()) {
    const QJsonObject stored = value.toObject();
    const QString text = stored.value(QStringLiteral("text")).toString();
    const std::optional<gp_Pnt> point = pointFrom(stored.value(QStringLiteral("point")).toArray(), 0);
    if (text.trimmed().isEmpty() || !point) {
      continue;
    }
    markup.addNote(
        {0, text.toStdString(), stored.value(QStringLiteral("part")).toString().toStdString(), *point});
  }
  for (const QJsonValue& value : object.value(QStringLiteral("strokes")).toArray()) {
    const QJsonObject stored = value.toObject();
    const QJsonArray coordinates = stored.value(QStringLiteral("points")).toArray();
    if (coordinates.size() % 3 != 0) {
      continue;
    }
    core::MarkupStroke stroke;
    stroke.part = stored.value(QStringLiteral("part")).toString().toStdString();
    bool ok = true;
    for (int i = 0; i < coordinates.size() && ok; i += 3) {
      const std::optional<gp_Pnt> point = pointFrom(coordinates, i);
      ok = point.has_value();
      if (ok) {
        stroke.points.push_back(*point);
      }
    }
    if (ok && stroke.points.size() >= 2) {
      markup.addStroke(std::move(stroke));
    }
  }
  return markup;
}

QJsonObject markupFileDocument(const core::Markup& markup, const QString& modelName) {
  return QJsonObject{{QStringLiteral("format"), FormatTag},
                     {QStringLiteral("version"), FormatVersion},
                     {QStringLiteral("model"), modelName},
                     {QStringLiteral("markup"), toJson(markup)}};
}

std::optional<MarkupFile> markupFromFileDocument(const QJsonObject& document, QString* error) {
  const auto fail = [error](const QString& reason) {
    if (error != nullptr) {
      *error = reason;
    }
    return std::nullopt;
  };
  if (document.value(QStringLiteral("format")).toString() != FormatTag) {
    return fail(QStringLiteral("The file is not a STEP Complete markup file."));
  }
  const int version = document.value(QStringLiteral("version")).toInt(0);
  if (version < 1) {
    return fail(QStringLiteral("The file has no format version."));
  }
  if (version > FormatVersion) {
    return fail(QStringLiteral("The file was written by a newer version of STEP Complete."));
  }
  return MarkupFile{markupFromJson(document.value(QStringLiteral("markup")).toObject()),
                    document.value(QStringLiteral("model")).toString()};
}

} // namespace sc::app
