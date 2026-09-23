// Small helpers for reading loosely-typed JSON the way the providers need to.
//
// Upstream payloads are someone else's data, so every field is read
// defensively: a wrong type costs that one field, never the whole response.
// The rules are the ones the providers have always applied:
//
// * a bool is never a number (`true` is not a count of 1);
// * a numeric-looking string still yields its number (`"17 skips"` -> 17);
// * text has its whitespace collapsed (upstream sends doubled spaces);
// * "truthiness" follows the usual rules: null, false, 0, "", [] and {} are
//   false; everything else is true.

#pragma once

#include <QDateTime>
#include <QJsonValue>
#include <QString>

#include <optional>

namespace mycu::json {

// The value's type as the error messages name it: "dict", "list", "str",
// "int", "float", "bool" or "NoneType".
QString typeName(const QJsonValue &value);

// Whether a value counts as true; `fallback` when it is missing entirely.
bool truthy(const QJsonValue &value, bool fallback = false);

// The value as text, the way it would print: "None" for null, "True"/"False"
// for bools, "17" for an integral number.
QString str(const QJsonValue &value);

// `str()`, with repr-style quotes around strings — for error messages that
// name the offending value.
QString repr(const QJsonValue &value);

// Whitespace-collapsed text; null and missing become "".
QString clean(const QJsonValue &value);

// A count: numbers truncate, numeric strings yield their first integer, bools
// and everything else are "not reported".
std::optional<int> asInt(const QJsonValue &value);

// A number that is really a number: not a bool, not a string.
std::optional<double> asNumber(const QJsonValue &value);

// Parse an ISO 8601 timestamp — `2026-08-20T10:00:00`, `…:29.623`,
// `…T14:00:00Z`, `…+05:00`, or a bare date. A timestamp with no zone is local
// time. Invalid (the "None" sentinel) for anything unreadable.
QDateTime parseIsoDateTime(const QString &text);

// `parseIsoDateTime` on a JSON value: falsy values and non-strings other than
// numbers give an invalid datetime.
QDateTime parseIsoDateTime(const QJsonValue &value);

} // namespace mycu::json
