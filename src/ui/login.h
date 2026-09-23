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
// 3. **If it is not**, the 302 to Microsoft fires. We show the surface, you
//    sign in, SAML posts you back, and `RelayState` lands you on the original
//    path — at which point we hide the surface again and continue.
//
// Success is therefore defined as one thing, checked one way: *the surface's
// URL is on the Self-Service origin and does not look like a sign-in page.* No
// cookie inspection, no timing assumptions, nothing that Entra can change
// under us.

#pragma once

#include "core/session.h"
#include "core/transport.h"

#include <QObject>

namespace mycu {

class WebBackend;

// Microsoft's federated sign-out endpoint. Navigating here ends the Entra
// session; `post_logout_redirect_uri` brings the browser back somewhere
// harmless so the user is not left staring at a Microsoft page.
inline const QString LOGOUT_URL =
    QStringLiteral("https://login.microsoftonline.com/81c32413-015d-4ba8-a93b-e1c28e355738"
                   "/oauth2/v2.0/logout?post_logout_redirect_uri=")
    + BASE_URL;

// Owns the login surface's visibility and decides when we are signed in.
//
// Exposed to QML as the context property `login`.
class LoginController : public QObject
{
    Q_OBJECT

    // Whether the WebView should be on screen.
    //
    // False during normal operation — the browser is an implementation detail
    // of the transport and the user should never see it. True only while an
    // interactive sign-in is in progress.
    Q_PROPERTY(bool surfaceVisible READ surfaceVisible NOTIFY surfaceVisibleChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

    // True once a sign-in has completed on this device.
    //
    // Only drives first-run copy ("Sign in to get started" vs "Reconnecting").
    // It is never treated as evidence that the session is still valid.
    Q_PROPERTY(bool hasLoggedInBefore READ hasLoggedInBefore NOTIFY sessionChanged)

public:
    // In `offline` (--demo) mode there is no browser and no session; the whole
    // flow is short-circuited so the UI does not claim to be "connecting".
    LoginController(SessionStore store, WebBackend *backend, bool offline = false,
                    QObject *parent = nullptr);

    bool surfaceVisible() const { return m_surfaceVisible; }
    QString status() const { return m_status; }
    bool hasLoggedInBefore() const { return m_state.hasLoggedIn(); }

public slots:
    // Navigate to `returnPath`, signing in first if necessary.
    //
    // Safe to call on a live session: the page simply loads and nothing is
    // shown to the user.
    void begin(const QString &returnPath);

    // React to the surface navigating. Wired to the QML surface's URL.
    //
    // This is the whole state machine. Three cases, in order:
    //
    // * **Sign-out in progress** and we are back on Self-Service — finish up.
    // * **On an identity provider** — show the surface; the user must act.
    // * **On Self-Service and not a sign-in page** — we are in. Hide the
    //   surface and tell whoever was waiting.
    void onUrlChanged(const QString &url);

    // A request came back from the identity provider. Re-run the flow.
    //
    // Identical to begin() except for the message, because the remedy for an
    // expired session is exactly the remedy for never having signed in.
    void onSessionExpired();

    // End the session: clear cookies and forget our metadata.
    //
    // On desktop this also empties QtWebEngine's cookie store directly. On
    // Android there is no cookie API, so the federated logout round trip is
    // the mechanism.
    void signOut();

signals:
    // The session is now good; whoever was waiting may retry their request.
    void loggedIn();
    // The surface must become visible so the user can complete a sign-in.
    void loginNeeded();
    // Sign-out finished; the app should return to its signed-out state.
    void signedOut();

    void surfaceVisibleChanged();
    void navigateRequested(const QString &url);
    void statusChanged();
    void sessionChanged();

private:
    void setSurfaceVisible(bool value);
    void setStatus(const QString &text);

    SessionStore m_store;
    SessionState m_state;
    WebBackend *m_backend;
    bool m_offline;
    bool m_surfaceVisible = false;
    QString m_status;
    QString m_returnPath;
    bool m_signingOut = false;
};

} // namespace mycu
