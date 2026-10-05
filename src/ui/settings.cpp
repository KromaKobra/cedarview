#include "settings.h"

#include "core/log.h"

namespace mycu {

SettingsController::SettingsController(QObject *parent)
    : QObject(parent)
{
    // QSettings has no typed read on every backend — the INI backend hands back
    // the string "true". Let Qt do the conversion to bool.
    m_light = m_settings.value(QLatin1StringView(LIGHT_MODE_KEY), false).toBool();
    m_avoid = m_settings.value(QLatin1StringView(AVOID_KEY)).toStringList();
    m_favorites = m_settings.value(QLatin1StringView(FAVORITES_KEY)).toStringList();
    m_recent = m_settings.value(QLatin1StringView(RECENT_SEARCHES_KEY)).toStringList();
    qCDebug(lcSettings).noquote() << QStringLiteral("settings: lightMode=%1 from %2")
                                         .arg(m_light ? QStringLiteral("true") : QStringLiteral("false"),
                                              m_settings.fileName());
}

void SettingsController::write(const char *key, const QVariant &value)
{
    m_settings.setValue(QLatin1StringView(key), value);
    m_settings.sync();
    emit changed();
}

void SettingsController::setLightMode(bool value)
{
    if (value == m_light)
        return;
    m_light = value;
    write(LIGHT_MODE_KEY, value);
}

void SettingsController::toggleLightMode()
{
    setLightMode(!m_light);
}

void SettingsController::setAvoid(const QStringList &allergens)
{
    if (allergens == m_avoid)
        return;
    m_avoid = allergens;
    write(AVOID_KEY, m_avoid);
}

void SettingsController::toggleFavorite(const QString &name)
{
    if (!m_favorites.removeOne(name))
        m_favorites.append(name);
    write(FAVORITES_KEY, m_favorites);
}

void SettingsController::addRecentSearch(const QString &query)
{
    const QString trimmed = query.simplified();
    if (trimmed.isEmpty())
        return;
    m_recent.removeIf([&](const QString &q) { return q.compare(trimmed, Qt::CaseInsensitive) == 0; });
    m_recent.prepend(trimmed);
    while (m_recent.size() > RECENT_SEARCH_LIMIT)
        m_recent.removeLast();
    write(RECENT_SEARCHES_KEY, m_recent);
}

void SettingsController::clearRecentSearches()
{
    if (m_recent.isEmpty())
        return;
    m_recent.clear();
    write(RECENT_SEARCHES_KEY, m_recent);
}

} // namespace mycu
