#include "cache.h"

#include "jsonfile.h"
#include "log.h"
#include "session.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>

#include <algorithm>

namespace mycu {

qint64 CachedPayload::ageSecs(const QDateTime &now) const
{
    return std::max<qint64>(0, savedAt.secsTo(now));
}

PayloadCache::PayloadCache(const QString &stateDir)
    : m_dir(QDir(stateDir.isEmpty() ? defaultStateDir() : stateDir).filePath(QStringLiteral("cache")))
{}

QString PayloadCache::pathFor(const QString &key) const
{
    return QDir(m_dir).filePath(key + QStringLiteral(".json"));
}

void PayloadCache::save(const QString &key, const QJsonValue &payload, const QDateTime &savedAt) const
{
    const QJsonObject envelope{
        {QStringLiteral("schema"), CACHE_SCHEMA},
        {QStringLiteral("saved_at"), savedAt.toMSecsSinceEpoch() / 1000.0},
        {QStringLiteral("payload"), payload},
    };
    if (atomicWriteJson(pathFor(key), envelope))
        qCDebug(lcSession).noquote() << "cache: saved" << key;
}

std::optional<CachedPayload> PayloadCache::load(const QString &key) const
{
    const std::optional<QJsonObject> envelope = readJsonObject(pathFor(key));
    if (!envelope)
        return std::nullopt;

    if (envelope->value(QStringLiteral("schema")).toInt(-1) != CACHE_SCHEMA) {
        qCInfo(lcSession).noquote() << "cache: ignoring" << key << "from another schema";
        return std::nullopt;
    }
    const double savedAt = envelope->value(QStringLiteral("saved_at")).toDouble(0.0);
    const QJsonValue payload = envelope->value(QStringLiteral("payload"));
    if (savedAt <= 0 || payload.isUndefined() || payload.isNull())
        return std::nullopt;

    return CachedPayload{payload, QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(savedAt * 1000))};
}

void PayloadCache::remove(const QString &key) const
{
    QFile::remove(pathFor(key));
}

void PayloadCache::clearPersonal() const
{
    for (const QString &key : PERSONAL_CACHE_KEYS)
        remove(key);
    qCInfo(lcSession) << "cache: personal records deleted";
}

void PayloadCache::clearAll() const
{
    QDir(m_dir).removeRecursively();
}

} // namespace mycu
