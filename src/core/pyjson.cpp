#include "pyjson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimeZone>

#include <cmath>
#include <limits>

namespace mycu::json {

namespace {

bool isIntegral(double d)
{
    return std::isfinite(d) && d == std::trunc(d)
        && std::fabs(d) < 9.0e15; // exactly representable as an integer
}

} // namespace

QString typeName(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        return QStringLiteral("NoneType");
    case QJsonValue::Bool:
        return QStringLiteral("bool");
    case QJsonValue::Double:
        return isIntegral(value.toDouble()) ? QStringLiteral("int") : QStringLiteral("float");
    case QJsonValue::String:
        return QStringLiteral("str");
    case QJsonValue::Array:
        return QStringLiteral("list");
    case QJsonValue::Object:
        return QStringLiteral("dict");
    }
    return QStringLiteral("object");
}

bool truthy(const QJsonValue &value, bool fallback)
{
    switch (value.type()) {
    case QJsonValue::Undefined:
        return fallback;
    case QJsonValue::Null:
        return false;
    case QJsonValue::Bool:
        return value.toBool();
    case QJsonValue::Double:
        return value.toDouble() != 0.0;
    case QJsonValue::String:
        return !value.toString().isEmpty();
    case QJsonValue::Array:
        return !value.toArray().isEmpty();
    case QJsonValue::Object:
        return !value.toObject().isEmpty();
    }
    return fallback;
}

QString str(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        return QStringLiteral("None");
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("True") : QStringLiteral("False");
    case QJsonValue::Double: {
        const double d = value.toDouble();
        if (isIntegral(d))
            return QString::number(static_cast<qint64>(d));
        return QString::number(d, 'g', QLocale::FloatingPointShortest);
    }
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Array:
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    case QJsonValue::Object:
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    return {};
}

QString repr(const QJsonValue &value)
{
    if (!value.isString())
        return str(value);
    QString escaped = value.toString();
    escaped.replace(u'\\', QStringLiteral("\\\\")).replace(u'\'', QStringLiteral("\\'"));
    return u'\'' + escaped + u'\'';
}

QString clean(const QJsonValue &value)
{
    if (value.isNull() || value.isUndefined())
        return {};
    return str(value).simplified();
}

std::optional<int> asInt(const QJsonValue &value)
{
    if (value.isNull() || value.isUndefined() || value.isBool())
        return std::nullopt;
    if (value.isDouble()) {
        const double d = value.toDouble();
        if (!std::isfinite(d))
            return std::nullopt;
        return static_cast<int>(std::trunc(d));
    }
    static const QRegularExpression firstInteger(QStringLiteral("-?\\d+"));
    const QRegularExpressionMatch match = firstInteger.match(str(value));
    if (!match.hasMatch())
        return std::nullopt;
    bool ok = false;
    const int parsed = match.captured().toInt(&ok);
    return ok ? std::optional<int>(parsed) : std::nullopt;
}

std::optional<double> asNumber(const QJsonValue &value)
{
    if (value.isDouble())
        return value.toDouble();
    return std::nullopt;
}

QDateTime parseIsoDateTime(const QString &raw)
{
    QString text = raw.trimmed();
    if (text.isEmpty())
        return {};

    // A bare date is midnight on that date.
    static const QRegularExpression dateOnly(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
    if (dateOnly.match(text).hasMatch()) {
        const QDate day = QDate::fromString(text, Qt::ISODate);
        return day.isValid() ? QDateTime(day, QTime(0, 0)) : QDateTime();
    }

    // "2026-08-20 10:00:00" is as good as the T form.
    static const QRegularExpression spaced(QStringLiteral("^(\\d{4}-\\d{2}-\\d{2}) (\\d)"));
    text.replace(spaced, QStringLiteral("\\1T\\2"));

    // The strict shape first, so that "next Tuesday-ish" and friends are
    // rejected rather than coaxed into something.
    static const QRegularExpression shape(QStringLiteral(
        "^(\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}(?::\\d{2})?)(\\.\\d+)?(Z|[+-]\\d{2}:?\\d{2})?$"));
    const QRegularExpressionMatch m = shape.match(text);
    if (!m.hasMatch())
        return {};

    // More than three fractional digits (.NET sends seven) is truncated to
    // milliseconds rather than refused.
    QString fraction = m.captured(2);
    if (fraction.size() > 4)
        fraction.truncate(4);
    const QString normalised = m.captured(1) + fraction + m.captured(3);

    const QDateTime parsed = QDateTime::fromString(normalised, Qt::ISODateWithMs);
    return parsed.isValid() ? parsed : QDateTime();
}

QDateTime parseIsoDateTime(const QJsonValue &value)
{
    if (!truthy(value))
        return {};
    return parseIsoDateTime(str(value));
}

} // namespace mycu::json
