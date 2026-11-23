#pragma once

#include "core/Markup.h"

#include <QJsonObject>
#include <QString>

#include <optional>

namespace sc::app {

// The JSON form of a model's markup (docs/designs/markup.md), as the per-model store keeps it
// under "markup":
//   {"notes": [{"text": "...", "part": "<pathId>", "point": [x, y, z]}],
//    "strokes": [{"part": "<pathId>", "points": [x0, y0, z0, x1, y1, z1, ...]}]}
// Points are in the part's coordinates (world for an empty part), in millimetres.
QJsonObject toJson(const core::Markup& markup);
// Skips entries that cannot be read (no text, bad points, a stroke of fewer than two points).
core::Markup markupFromJson(const QJsonObject& object);

// The Export Markup file: the same markup with a format tag, a version and the model's file name.
QJsonObject markupFileDocument(const core::Markup& markup, const QString& modelName);
struct MarkupFile {
  core::Markup markup;
  QString modelName; // the file name of the model it was exported from
};
// Nothing (with a reason) for a document that is not a markup file or is from a newer version.
std::optional<MarkupFile> markupFromFileDocument(const QJsonObject& document, QString* error = nullptr);

} // namespace sc::app
