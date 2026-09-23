// UI preferences, and the single source of truth for which theme is on.
//
// Backed by QSettings, **not** by SessionStore. The session store is thrown
// away when you sign out; a theme choice surviving a sign-out is the behaviour
// anyone would expect, and coupling the two would mean the app forgot your
// palette every time the SAML session expired. QSettings also already knows
// where to write on each platform — ~/.config/Kroma/CedarView.conf on Linux,
// app-private storage on Android — which is one less path to resolve by hand.
//
// The default constructor works because main.cpp sets the organisation and
// application names before this is built. Constructing it earlier would
// silently write to a file named after the executable.
//
// **Why QML reads this rather than a property on Theme:** Theme.qml is a plain
// QtObject instantiated once per file, not a singleton (its own header comment
// explains why). Eight independent instances need one shared answer, and a
// context property is the cheapest thing that is genuinely shared — every Theme
// binds `light` to `settings.lightMode` and they all change together.
//
// Exposed to QML as the context property `settings`.

#pragma once

#include <QObject>
#include <QSettings>

namespace mycu {

// One key, one group. The group exists so the next preference has an obvious
// home and does not end up at the top level next to Qt's own bookkeeping.
inline constexpr const char *LIGHT_MODE_KEY = "ui/lightMode";

class SettingsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool lightMode READ lightMode WRITE setLightMode NOTIFY changed)

public:
    explicit SettingsController(QObject *parent = nullptr);

    bool lightMode() const { return m_light; }

public slots:
    // Set the theme and write it through immediately.
    //
    // sync() rather than waiting for the destructor: on Android the process is
    // killed rather than exited, and a preference that only lands on a clean
    // shutdown is a preference that mostly does not land.
    void setLightMode(bool value);
    void toggleLightMode();

signals:
    void changed();

private:
    QSettings m_settings;
    bool m_light = false;
};

} // namespace mycu
