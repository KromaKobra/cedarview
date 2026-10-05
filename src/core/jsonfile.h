// Small JSON files in the app's private storage, written so that a crash can
// never leave half of one behind.
//
// Shared by SessionStore (session.json) and PayloadCache (cache/*.json). Both
// are read forgivingly: a missing, corrupt or wrong-shaped file is "nothing
// saved", because the cost of that is one more request, and the cost of
// crashing on startup is an app that cannot recover without a terminal.

#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace mycu {

// Write `object` to `path` atomically and 0600: a temporary file beside it,
// then rename(2) over the target. Creates the parent directory 0700 if it is
// missing. False, with a warning, when the file could not be written.
bool atomicWriteJson(const QString &path, const QJsonObject &object,
                     QJsonDocument::JsonFormat format = QJsonDocument::Compact);

// The object stored at `path`, or nothing when it is missing, unreadable, not
// JSON, or not an object.
std::optional<QJsonObject> readJsonObject(const QString &path);

// Create `dir` 0700 if it is not already there.
void ensurePrivateDir(const QString &dir);

} // namespace mycu
