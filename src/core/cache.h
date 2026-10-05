// The on-device copy of what was last loaded, so the app opens on figures
// rather than on dashes.
//
// What is kept is the **raw payload** each service sent — the chapel
// endpoints' JSON, the balance JSON, the menu and schedule feeds — never the
// parsed models. On load the providers' own parsers (buildSummary,
// parseBalance, parseMenus, parseUpcoming) run over it again. So there is no
// second serialiser to keep in step with the models, and a parser fix applies
// to data saved before it.
//
// One file per source, `<stateDir>/cache/<key>.json`, 0600 and written
// atomically, beside session.json:
//
//     {"schema": 1, "saved_at": 1789650000.5, "payload": <what the service sent>}
//
// Two of the keys are **personal** — the chapel ledger and the meal plan come
// from behind the Cedarville sign-in — and are deleted on sign-out
// (clearPersonal). The menu and the chapel schedule are public and survive it.

#pragma once

#include <QDateTime>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <optional>

namespace mycu {

namespace cachekey {
inline const QString CHAPEL = QStringLiteral("chapel");
inline const QString MEALS = QStringLiteral("meals");
inline const QString MENUS = QStringLiteral("menus");
inline const QString SCHEDULE = QStringLiteral("schedule");
} // namespace cachekey

// The keys that hold someone's own records.
inline const QStringList PERSONAL_CACHE_KEYS = {cachekey::CHAPEL, cachekey::MEALS};

// Bumped when the envelope changes shape. A file with another value is
// ignored: it costs one fetch.
inline constexpr int CACHE_SCHEMA = 1;

struct CachedPayload
{
    QJsonValue payload;
    QDateTime savedAt;

    // Seconds since it was saved; negative clocks (a phone whose time moved
    // back) count as just saved.
    qint64 ageSecs(const QDateTime &now = QDateTime::currentDateTime()) const;
};

class PayloadCache
{
public:
    // An empty `stateDir` means defaultStateDir().
    explicit PayloadCache(const QString &stateDir = QString());

    QString dir() const { return m_dir; }
    QString pathFor(const QString &key) const;

    // Keep `payload` as the latest for `key`. A failure to write is logged and
    // otherwise ignored — the cache is an optimisation, never a dependency.
    void save(const QString &key, const QJsonValue &payload,
              const QDateTime &savedAt = QDateTime::currentDateTime()) const;

    // The latest payload for `key`, or nothing when there is none or the file
    // is unreadable, from another schema, or has no timestamp.
    std::optional<CachedPayload> load(const QString &key) const;

    void remove(const QString &key) const;
    void clearPersonal() const;
    void clearAll() const;

private:
    QString m_dir;
};

} // namespace mycu
