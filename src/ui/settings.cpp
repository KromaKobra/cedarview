#include "settings.h"

#include "core/log.h"

namespace mycu {

SettingsController::SettingsController(QObject *parent)
    : QObject(parent)
{
    // QSettings has no typed read on every backend — the INI backend hands back
    // the string "true". Let Qt do the conversion to bool.
    m_light = m_settings.value(QLatin1StringView(LIGHT_MODE_KEY), false).toBool();
    qCDebug(lcSettings).noquote() << QStringLiteral("settings: lightMode=%1 from %2")
                                         .arg(m_light ? QStringLiteral("true") : QStringLiteral("false"),
                                              m_settings.fileName());
}

void SettingsController::setLightMode(bool value)
{
    if (value == m_light)
        return;
    m_light = value;
    m_settings.setValue(QLatin1StringView(LIGHT_MODE_KEY), value);
    m_settings.sync();
    emit changed();
}

void SettingsController::toggleLightMode()
{
    setLightMode(!m_light);
}

} // namespace mycu
