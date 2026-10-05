// Session state: where it lives on disk, and what little of it we own.
//
// An important thing to be clear about: **this does not store cookies.** The
// session cookie is `HttpOnly`, and on Android it is unreachable from Qt (the
// `cookieAdded` signal only fires for cookies *we* set, which is exactly why
// the transport lets the WebView make the request instead of harvesting the
// cookie). The cookie jar is owned by the WebView:
//
// * **Desktop** — QtWebEngine's persistent profile, stored under
//   SessionStore::profileDir().
// * **Android** — the system WebView's own cookie store, in app-private
//   storage. We neither see it nor manage it.
//
// What this does own is the small amount of metadata that makes the UI behave
// sensibly across launches: have we ever logged in, when did a request last
// succeed, and which term were we last looking at. It is written 0600 under
// `$XDG_DATA_HOME` (or `filesDir` on Android) and never goes near git.
//
// The app never sees or stores your password. Login happens on Microsoft's own
// page, inside the WebView.

#pragma once

#include <QString>

#include <functional>

namespace mycu {

inline const QString APP_DIR_NAME = QStringLiteral("mycu");

// Bumped whenever the on-disk shape of `session.json` changes. A file with an
// unknown value is discarded rather than migrated — it holds nothing expensive
// to regenerate — with one exception: version 1 is read as it is, because v2
// only added fields, and dropping v1's `last_login` would show every existing
// user the first-run Welcome screen again.
inline constexpr int SCHEMA_VERSION = 2;

// Non-secret metadata about the current session.
//
// Nothing here is a credential. If this file leaked it would reveal that you
// use the app and roughly when — which is why it is still written 0600, but
// also why losing it costs you nothing more than one extra login.
struct SessionState
{
    // Unix time of the last completed interactive login, 0 if never.
    double lastLogin = 0.0;
    // Unix time of the last request that came back with real data.
    double lastSuccess = 0.0;
    // Unix time we last detected expiry, for "your session ended" messaging.
    double lastExpiry = 0.0;
    // Whatever the chapel view was last showing, so a cold start looks right.
    QString lastTerm;

    // Who Self-Service's pages look up, remembered so a refresh can skip the
    // page that names them: the chapel dashboard (59 KB) for `studentId`, and
    // the meal-plan page for the other two. Identifiers, not credentials —
    // every one of them is useless without the session cookie, which this
    // file never holds. Forgotten on sign-out with everything else here.
    QString studentId;
    QString mealsPersonId;
    QString mealsCard;

    // Copy of SCHEMA_VERSION at write time; see SessionStore::load().
    int schema = SCHEMA_VERSION;

    // Read from a version 1 file, which v0.3 wrote. v0.3 could lose
    // `lastLogin` to the stale-copy bug update() describes, so a v1 file
    // without one may still belong to a device the WebView is signed in on.
    // Not saved: whatever is written back is version 2.
    bool fromV1 = false;

    // Whether we have ever completed a login on this device.
    //
    // Drives first-run behaviour only. It is *not* a claim that the session is
    // still valid — that is never predicted, only discovered by making a
    // request. See looksLikeLogin().
    bool hasLoggedIn() const { return lastLogin > 0; }
};

// Where session state belongs on this platform.
//
// `MYCU_STATE_DIR` overrides it, for tests and for running two profiles side
// by side. Otherwise it is `$XDG_DATA_HOME/mycu` (or `~/.local/share/mycu`) on
// desktop — the path it has always had, so an existing sign-in survives — and
// `<filesDir>/mycu` on Android, which is app-private.
QString defaultStateDir();

// Loads, saves and clears SessionState.
//
// Deliberately forgiving on read: a corrupt or older-schema file is treated as
// "no session", because the cost of being wrong is one login, and the cost of
// crashing on startup is an app that cannot recover without a terminal.
class SessionStore
{
public:
    static constexpr const char *FILENAME = "session.json";

    // An empty `stateDir` means defaultStateDir().
    explicit SessionStore(const QString &stateDir = QString());

    QString stateDir() const { return m_stateDir; }
    QString path() const;

    // Directory for the WebView's persistent cookie jar (desktop only).
    //
    // Handed to QQuickWebEngineProfile::setPersistentStoragePath. Android
    // ignores this entirely — the system WebView manages its own store.
    QString profileDir() const;

    // Create the state directory 0700 if it is not already there.
    void ensureDirs() const;

    SessionState load() const;

    // Write state atomically and 0600.
    //
    // Atomic because a half-written file on a crash would otherwise be read
    // back as "no session" — recoverable, but an avoidable surprise.
    void save(const SessionState &state) const;

    // Read the file, apply `change`, write it back, and return the result.
    //
    // Every partial write goes through here. The login controller and the
    // chapel viewmodel each used to keep their own copy of the state and save
    // the whole struct, so chapel's "last success" wrote back the `lastLogin`
    // it had read at startup — 0 on a first run — over the sign-in the login
    // controller had just recorded. Reloading first means a writer can only
    // change the fields it means to. (Every writer is on the GUI thread, so
    // read-modify-write needs no lock.)
    SessionState update(const std::function<void(SessionState &)> &change) const;

    SessionState markLogin() const;
    SessionState markSuccess() const;
    SessionState markExpiry() const;

    // Forget the session metadata.
    //
    // Does **not** clear cookies — that is the WebView's job, and the UI does
    // it through LoginController::signOut() so both halves are dropped
    // together.
    void clear() const;

private:
    QString m_stateDir;
};

} // namespace mycu
