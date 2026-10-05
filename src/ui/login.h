// Interactive login, and how we know it worked.
//
// Why a WebView and not a token flow
// ----------------------------------
//
// `selfservice.cedarville.edu` is a SAML 2.0 Service Provider federated to
// Entra ID (tenant `81c32413-015d-4ba8-a93b-e1c28e355738`). Verified live:
// requesting `/cedarinfo/chapelskip` unauthenticated returns
//
//     302 https://login.microsoftonline.com/81c32413-…/saml2?SAMLRequest=…&RelayState=%2Fcedarinfo%2Fchapelskip
//
// There is no OAuth client we could register against it, so MSAL and every
// token-based flow are out. Interactive login in an embedded browser is the
// only workable approach — and it has a real advantage: **the app never sees
// your password.** You type it on Microsoft's own page, MFA included, and we
// only ever observe which URL the browser ended up on.
//
// The flow
// --------
//
// `RelayState` carries the return path, so we do not need a separate "login
// page" concept at all:
//
// 1. Point the surface at the target path (e.g. `/cedarinfo/chapelskip`).
// 2. **If the session is alive**, it loads. The surface stays hidden and we go
//    straight to fetching data.
// 3. **If it is not**, the 302 to Microsoft fires, you sign in, SAML posts you
//    back, and `RelayState` lands you on the original path.
//
// Success is therefore the surface being on the Self-Service origin, and
// nowhere that looks like a sign-in page, in one of two ways:
//
// * a Self-Service page has **finished loading** (onPageLoaded) — step 2, or
//   the end of step 3;
// * while signing in, the surface has **come back from Microsoft** — whose
//   page loaded since the surface was sent off — to Self-Service
//   (onUrlChanged), so the page can go the moment it does rather than once
//   the dashboard behind it has loaded.
//
// Never the address merely being Self-Service's. Both surfaces report the
// address they were asked for the moment they are asked, before Self-Service
// has had the chance to send them to Microsoft — so "on Self-Service" alone
// declared every sign-in finished before it had begun, and the first fetch
// then found no session. No cookie inspection, nothing Entra can change under
// us.
//
// The phases
// ----------
//
// Microsoft's page appears only when the user asks for it, inside CedarView's
// own chrome. Everything else is one of these:
//
//   welcome      no session on this device; CedarView's own first screen
//   checking     a silent check of a session we had: the surface goes to
//                Self-Service out of sight, and if it lands on Microsoft it
//                is given a few seconds (GRACE_MS) to come back on its own —
//                an Entra single-sign-on round trip does — before giving up
//   signedIn     the session is good
//   needsSignIn  the session ended and the silent check could not renew it;
//                the saved figures stay on screen under a banner
//   signingIn    the sign-in surface is showing, because the user tapped
//                Sign in
//   preview      sample data, no session involved
//
// Signing out is not a phase: it empties the WebView's cookie jar (Microsoft's
// cookies and Self-Service's alike) and is back at Welcome at once. See
// signOut().
//
// Main.qml switches on `phase`. The fetching itself belongs to
// SyncCoordinator, which listens to the signals below.

#pragma once

#include "core/session.h"
#include "core/transport.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace mycu {

class WebBackend;

// How long a silent check may sit on Microsoft's page before the app stops
// waiting for single sign-on to bring it back. Long enough for an Entra
// redirect round trip, short enough that nobody is left looking at a
// skeleton.
inline constexpr int GRACE_MS = 5000;

// How long a silent check may go without reaching either Microsoft or
// Self-Service — the page simply not loading, which offline is — before the
// app stops waiting and lets the fetches report what is wrong.
inline constexpr int CHECK_TIMEOUT_MS = 20000;

// How soon after a silent check renewed the session another expiry stops
// being worth a silent check. The renewal evidently did not take — Self-Service
// still turns the fetches away — and checking again would only go round the
// same loop, out of sight, for as long as the app was open.
inline constexpr int RENEWAL_COOLDOWN_MS = 60000;

// Owns the phase, the sign-in surface's visibility, and decides when we are
// signed in.
//
// Exposed to QML as the context property `login`.
class LoginController : public QObject
{
    Q_OBJECT

    // "welcome" | "checking" | "signedIn" | "needsSignIn" | "signingIn" |
    // "preview". A string, so QML can compare it without the type being
    // registered.
    Q_PROPERTY(QString phase READ phaseName NOTIFY phaseChanged)

    // Whether the WebView should be on screen: while signing in.
    Q_PROPERTY(bool surfaceVisible READ surfaceVisible NOTIFY phaseChanged)
    Q_PROPERTY(bool inPreview READ inPreview NOTIFY phaseChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

    // True once a sign-in has completed on this device.
    //
    // Only decides what the app opens on. It is never treated as evidence
    // that the session is still valid.
    Q_PROPERTY(bool hasLoggedInBefore READ hasLoggedInBefore NOTIFY sessionChanged)

public:
    enum class Phase { Welcome, Checking, SignedIn, NeedsSignIn, SigningIn, Preview };
    Q_ENUM(Phase)

    // In `demo` (--demo) mode there is no browser and no session: the app
    // starts in preview and sign-in is unavailable.
    LoginController(SessionStore store, WebBackend *backend, bool demo = false, QObject *parent = nullptr);

    Phase phase() const { return m_phase; }
    QString phaseName() const;
    bool surfaceVisible() const { return m_phase == Phase::SigningIn; }
    bool inPreview() const { return m_phase == Phase::Preview; }
    QString status() const { return m_status; }
    bool hasLoggedInBefore() const { return m_state.hasLoggedIn(); }

    // Whether a device that opens on Welcome may still hold a session in its
    // WebView: one that last ran v0.3, which could lose its record of the
    // sign-in (see SessionState::fromV1). Never after a sign-out, and never on
    // a fresh install — neither has a session to find.
    bool mayHoldForgottenSession() const { return m_forgottenSession; }

    // How the app starts.
    //
    // `returnPath` is where the surface goes, and where a sign-in lands.
    // `nativeSession` says personal data can be fetched without the WebView
    // having loaded anything (Android, through CookieManager): a session we
    // had is then assumed good, and the first fetch finds out. Otherwise it is
    // checked silently first.
    void start(const QString &returnPath, bool nativeSession);

    // Timings, as seams for tests.
    void setTimings(int graceMs, int checkTimeoutMs);

public slots:
    // The user asked to sign in: from Welcome, the "session ended" banner, or
    // sample data. Shows the surface as soon as Microsoft's page is reached.
    void startSignIn();
    // Back out of the sign-in surface: to the saved data if there is a past
    // session, else to Welcome.
    void cancelSignIn();

    void startPreview();
    // Leave sample data: back to Welcome, or to the user's own data.
    void exitPreview();

    // React to the surface navigating. Wired to the QML surface's URL.
    void onUrlChanged(const QString &url);
    // React to a page having finished loading. Wired to the QML surface's
    // pageLoaded().
    void onPageLoaded(const QString &url);

    // A request came back from the identity provider. When signed in, check
    // silently whether single sign-on renews it — unless a silent check
    // renewed it moments ago (RENEWAL_COOLDOWN_MS), which evidently did not
    // take: then straight to needsSignIn. Otherwise there is nothing to do (a
    // check is already under way, or nobody is signed in).
    void onSessionExpired();

    // A personal fetch succeeded, which proves the session: whatever phase
    // was waiting on that becomes signedIn. This is how Android's first run
    // skips Welcome when the WebView already holds a session.
    void confirmSignedIn();

    // Try the silent check again — pull-to-refresh under the "session ended"
    // banner.
    void checkAgain();

    // End the session, here and now: empty the WebView's cookie jar, forget
    // our metadata, drop the signed-in student's saved records (signedOut),
    // and go to Welcome.
    //
    // There is no trip to Microsoft's logout page. With the cookies gone it
    // has nothing to act on — the jar was always emptied first — and all it
    // ever did was keep Microsoft's sign-in page on screen for up to fifteen
    // seconds, which looked for all the world like being signed back in. The
    // next sign-in starts from an empty jar and asks for the account and
    // password.
    void signOut();

signals:
    // The session is good, newly: after a sign-in or a silent check. Whoever
    // was waiting may fetch.
    void loggedIn();
    // Signed out: the app is at Welcome, and the personal data must go.
    void signedOut();
    void previewChanged(bool on);

    void phaseChanged();
    void navigateRequested(const QString &url);
    void statusChanged();
    void sessionChanged();

private:
    void setPhase(Phase phase);
    void setStatus(const QString &text);
    void beginCheck();
    void succeed();
    void onGraceExpired();
    void onCheckTimedOut();
    // Point the surface at the return path, as a new attempt.
    void navigateToReturnPath();
    void clearCookies();

    SessionStore m_store;
    SessionState m_state;
    WebBackend *m_backend;
    bool m_demo;
    bool m_nativeSession = false;
    Phase m_phase = Phase::Welcome;
    QString m_status;
    QString m_returnPath = QStringLiteral("/");
    bool m_forgottenSession = false;
    // A Microsoft page has loaded since the surface was last sent to the
    // return path.
    bool m_reachedIdp = false;
    // Since a silent check last brought the session back.
    QElapsedTimer m_renewed;
    // A silent renewal was refused straight away: the cookies the WebView
    // holds are no good, and the next sign-in starts without them.
    bool m_clearCookiesFirst = false;

    QTimer m_grace;
    QTimer m_checkTimeout;
};

} // namespace mycu
