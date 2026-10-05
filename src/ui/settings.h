// UI preferences, and the single source of truth for which theme is on.
//
// Backed by QSettings, **not** by SessionStore. The session store is thrown
// away when you sign out; a theme choice surviving a sign-out is the behaviour
// anyone would expect, and coupling the two would mean the app forgot your
// palette every time the SAML session expired. QSettings also already knows
// where to write on each platform — ~/.config/Kroma/CedarView.conf on Linux,
// app-private storage on Android — which is one less path to resolve by hand.
//
// The same goes for the other preferences kept here: the Menu section's Avoid
// chips, starred buildings, and recent searches. None of them is a record of
// anyone's account, so sign-out leaves them be.
//
// The default constructor works because main.cpp sets the organisation and
// application names before this is built. Constructing it earlier would
// silently write to a file named after the executable.
//
// **Why QML reads this rather than a property on Theme:** Theme.qml is a plain
// QtObject instantiated once per file, not a singleton (its own header comment
// explains why). Many independent instances need one shared answer, and a
// context property is the cheapest thing that is genuinely shared — every Theme
// binds `light` to `settings.lightMode` and they all change together.
//
// Exposed to QML as the context property `settings`.

#pragma once

#include <QObject>
#include <QSettings>
#include <QStringList>

namespace mycu {

// Keys under a group each, so the next preference has an obvious home and
// does not end up at the top level next to Qt's own bookkeeping.
inline constexpr const char *LIGHT_MODE_KEY = "ui/lightMode";
// The Menu section's Avoid chips: allergen labels, as the feed spells them.
inline constexpr const char *AVOID_KEY = "menu/avoid";
// Buildings starred on the Campus tab, by name.
inline constexpr const char *FAVORITES_KEY = "campus/favorites";
// The last few searches, newest first.
inline constexpr const char *RECENT_SEARCHES_KEY = "search/recent";

// How many recent searches are kept.
inline constexpr int RECENT_SEARCH_LIMIT = 5;

class SettingsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool lightMode READ lightMode WRITE setLightMode NOTIFY changed)
    Q_PROPERTY(QStringList favorites READ favorites NOTIFY changed)
    Q_PROPERTY(QStringList recentSearches READ recentSearches NOTIFY changed)

public:
    explicit SettingsController(QObject *parent = nullptr);

    bool lightMode() const { return m_light; }
    QStringList avoid() const { return m_avoid; }
    QStringList favorites() const { return m_favorites; }
    QStringList recentSearches() const { return m_recent; }

    // Written by DiningViewModel, which owns the chips; kept here so they
    // survive a relaunch.
    void setAvoid(const QStringList &allergens);

public slots:
    // Set the theme and write it through immediately.
    //
    // sync() rather than waiting for the destructor: on Android the process is
    // killed rather than exited, and a preference that only lands on a clean
    // shutdown is a preference that mostly does not land. Every setter here
    // does the same.
    void setLightMode(bool value);
    void toggleLightMode();

    void toggleFavorite(const QString &name);
    bool isFavorite(const QString &name) const { return m_favorites.contains(name); }

    // Remember `query` as the newest search. Blank queries are not kept, and a
    // repeat moves to the front rather than appearing twice.
    void addRecentSearch(const QString &query);
    void clearRecentSearches();

signals:
    void changed();

private:
    void write(const char *key, const QVariant &value);

    QSettings m_settings;
    bool m_light = false;
    QStringList m_avoid;
    QStringList m_favorites;
    QStringList m_recent;
};

} // namespace mycu
