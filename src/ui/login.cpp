#include "login.h"

#include "core/log.h"
#include "platform/backend.h"

namespace mycu {

LoginController::LoginController(SessionStore store, WebBackend *backend, bool offline,
                                 QObject *parent)
    : QObject(parent)
    , m_store(std::move(store))
    , m_state(m_store.load())
    , m_backend(backend)
    , m_offline(offline)
{}

void LoginController::setSurfaceVisible(bool value)
{
    if (m_surfaceVisible != value) {
        m_surfaceVisible = value;
        emit surfaceVisibleChanged();
    }
}

void LoginController::setStatus(const QString &text)
{
    if (m_status != text) {
        m_status = text;
        emit statusChanged();
        qCInfo(lcLogin).noquote() << "login:" << text;
    }
}

void LoginController::begin(const QString &returnPath)
{
    m_returnPath = returnPath.isEmpty() ? QStringLiteral("/") : returnPath;
    m_signingOut = false;

    if (m_offline) {
        setStatus(QStringLiteral("Demo mode — showing saved fixtures"));
        return;
    }

    setStatus(QStringLiteral("Connecting to Cedarville…"));
    emit navigateRequested(resolve(m_returnPath));
}

void LoginController::onUrlChanged(const QString &url)
{
    if (url.isEmpty())
        return;

    if (m_signingOut) {
        // Only the landing back on Self-Service ends the sign-out. Matching on
        // anything in the logout URL itself (it carries
        // `post_logout_redirect_uri`) would declare victory the instant we
        // navigated, before Entra had actually dropped the session.
        if (isSelfservice(url)) {
            m_signingOut = false;
            setSurfaceVisible(false);
            setStatus(QStringLiteral("Signed out."));
            emit signedOut();
        }
        return;
    }

    if (looksLikeLogin(url)) {
        if (!m_surfaceVisible) {
            setStatus(QStringLiteral("Sign in with your Cedarville account"));
            setSurfaceVisible(true);
            emit loginNeeded();
        }
        return;
    }

    if (isSelfservice(url)) {
        const bool wasVisible = m_surfaceVisible;
        setSurfaceVisible(false);
        setStatus(QString());
        if (wasVisible || !m_state.hasLoggedIn()) {
            m_store.markLogin(m_state);
            emit sessionChanged();
        }
        emit loggedIn();
    }
}

void LoginController::onSessionExpired()
{
    m_store.markExpiry(m_state);
    begin(m_returnPath.isEmpty() ? QStringLiteral("/") : m_returnPath);
    // After begin(), which sets its own first-connect message. If the session
    // is in fact still good this is replaced again the moment the page loads,
    // so the user never sees it for a non-event.
    setStatus(QStringLiteral("Your session ended. Signing in again…"));
}

void LoginController::signOut()
{
    m_signingOut = true;
    setStatus(QStringLiteral("Signing out…"));
    try {
        if (m_backend)
            m_backend->clearCookies();
    } catch (const std::exception &e) {
        // Never let sign-out crash.
        qCWarning(lcLogin) << "backend cookie clear failed (continuing):" << e.what();
    }

    m_store.clear();
    m_state = m_store.load();
    emit sessionChanged();
    setSurfaceVisible(true);
    emit navigateRequested(LOGOUT_URL);
}

} // namespace mycu
