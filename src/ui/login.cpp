#include "login.h"

#include "core/log.h"
#include "platform/backend.h"

namespace mycu {

LoginController::LoginController(SessionStore store, WebBackend *backend, bool demo, QObject *parent)
    : QObject(parent)
    , m_store(std::move(store))
    , m_state(m_store.load())
    , m_backend(backend)
    , m_demo(demo)
    , m_forgottenSession(m_state.fromV1 && !m_state.hasLoggedIn())
{
    m_grace.setSingleShot(true);
    m_checkTimeout.setSingleShot(true);
    setTimings(GRACE_MS, CHECK_TIMEOUT_MS);
    connect(&m_grace, &QTimer::timeout, this, &LoginController::onGraceExpired);
    connect(&m_checkTimeout, &QTimer::timeout, this, &LoginController::onCheckTimedOut);
}

void LoginController::setTimings(int graceMs, int checkTimeoutMs)
{
    m_grace.setInterval(graceMs);
    m_checkTimeout.setInterval(checkTimeoutMs);
}

QString LoginController::phaseName() const
{
    switch (m_phase) {
    case Phase::Welcome:
        return QStringLiteral("welcome");
    case Phase::Checking:
        return QStringLiteral("checking");
    case Phase::SignedIn:
        return QStringLiteral("signedIn");
    case Phase::NeedsSignIn:
        return QStringLiteral("needsSignIn");
    case Phase::SigningIn:
        return QStringLiteral("signingIn");
    case Phase::Preview:
        return QStringLiteral("preview");
    }
    return {};
}

void LoginController::setPhase(Phase phase)
{
    if (phase != Phase::Checking) {
        m_grace.stop();
        m_checkTimeout.stop();
    }
    if (m_phase == phase)
        return;
    m_phase = phase;
    qCInfo(lcLogin).noquote() << "login: phase" << phaseName();
    emit phaseChanged();
}

void LoginController::setStatus(const QString &text)
{
    if (m_status != text) {
        m_status = text;
        emit statusChanged();
        if (!text.isEmpty())
            qCInfo(lcLogin).noquote() << "login:" << text;
    }
}

void LoginController::start(const QString &returnPath, bool nativeSession)
{
    m_returnPath = returnPath.isEmpty() ? QStringLiteral("/") : returnPath;
    m_nativeSession = nativeSession;

    if (m_demo) {
        startPreview();
        return;
    }
    if (!m_state.hasLoggedIn()) {
        setPhase(Phase::Welcome);
        return;
    }
    if (nativeSession) {
        // Nothing to load: the first fetch says whether the session is good.
        setPhase(Phase::SignedIn);
        return;
    }
    beginCheck();
}

void LoginController::beginCheck()
{
    setPhase(Phase::Checking);
    setStatus(QStringLiteral("Checking your sign-in…"));
    m_grace.stop();
    m_checkTimeout.start();
    navigateToReturnPath();
}

void LoginController::navigateToReturnPath()
{
    m_reachedIdp = false;
    emit navigateRequested(resolve(m_returnPath));
}

void LoginController::clearCookies()
{
    try {
        if (m_backend)
            m_backend->clearCookies();
    } catch (const std::exception &e) {
        // Never let sign-in or sign-out crash.
        qCWarning(lcLogin) << "backend cookie clear failed (continuing):" << e.what();
    }
}

void LoginController::startSignIn()
{
    if (m_demo) {
        setStatus(QStringLiteral("Sign-in isn't available with --demo."));
        return;
    }
    if (m_phase == Phase::SigningIn)
        return;

    if (m_clearCookiesFirst) {
        qCInfo(lcLogin) << "login: starting this sign-in from an empty cookie jar";
        clearCookies();
        m_clearCookiesFirst = false;
    }
    const bool leavingPreview = m_phase == Phase::Preview;
    setPhase(Phase::SigningIn);
    if (leavingPreview)
        emit previewChanged(false);
    setStatus(QStringLiteral("Sign in with your Cedarville account"));
    navigateToReturnPath();
}

void LoginController::cancelSignIn()
{
    if (m_phase != Phase::SigningIn)
        return;
    setStatus(QString());
    setPhase(m_state.hasLoggedIn() ? Phase::NeedsSignIn : Phase::Welcome);
}

void LoginController::startPreview()
{
    if (m_phase == Phase::Preview)
        return;
    setStatus(QString());
    setPhase(Phase::Preview);
    emit previewChanged(true);
}

void LoginController::exitPreview()
{
    if (m_phase != Phase::Preview)
        return;
    // The phase first, so whoever reacts to leaving preview sees where to.
    if (m_demo || !m_state.hasLoggedIn())
        setPhase(Phase::Welcome);
    else if (m_nativeSession)
        setPhase(Phase::SignedIn);
    else
        beginCheck();
    emit previewChanged(false);
}

void LoginController::onUrlChanged(const QString &url)
{
    if (url.isEmpty())
        return;

    if (looksLikeLogin(url)) {
        // A silent check that reached Microsoft gets a moment for single
        // sign-on to bring it back before anyone is asked to do anything.
        if (m_phase == Phase::Checking && !m_grace.isActive())
            m_grace.start();
        return;
    }

    // Back from Microsoft's page: the sign-in is done, and the page on screen
    // can go now rather than once Self-Service's has loaded behind it. Only
    // after Microsoft — before it, Self-Service's address is just the one we
    // asked for. A silent check has nothing on screen to hurry, and waits for
    // the page to load (onPageLoaded), by which time the session cookie is
    // certainly in the jar.
    if (m_phase == Phase::SigningIn && m_reachedIdp && isSelfservice(url))
        succeed();
}

void LoginController::onPageLoaded(const QString &url)
{
    if (looksLikeLogin(url)) {
        // Taken from a page that has loaded, never from the address alone:
        // Android's surface, shown, first reports the page it was already on —
        // Microsoft's, when a check has just given up there — before the one
        // it was sent to.
        m_reachedIdp = true;
        return;
    }
    if (!isSelfservice(url))
        return;
    // NeedsSignIn too: a check whose single sign-on came back after the grace
    // ran out.
    if (m_phase == Phase::Checking || m_phase == Phase::SigningIn || m_phase == Phase::NeedsSignIn)
        succeed();
}

void LoginController::succeed()
{
    const bool interactive = m_phase == Phase::SigningIn;
    setStatus(QString());
    setPhase(Phase::SignedIn);
    if (interactive)
        m_renewed.invalidate();
    else
        m_renewed.start();
    if (interactive || !m_state.hasLoggedIn()) {
        m_state = m_store.markLogin();
        emit sessionChanged();
    }
    emit loggedIn();
}

void LoginController::onGraceExpired()
{
    if (m_phase != Phase::Checking)
        return;
    setStatus(QStringLiteral("Your session ended"));
    setPhase(Phase::NeedsSignIn);
}

void LoginController::onCheckTimedOut()
{
    if (m_phase != Phase::Checking || m_grace.isActive())
        return;
    // Neither Microsoft nor Self-Service answered — offline, most likely. Stop
    // waiting, and let the fetches report what is actually wrong.
    qCInfo(lcLogin) << "login: the silent check got no answer; carrying on with the saved session";
    setStatus(QString());
    setPhase(Phase::SignedIn);
}

void LoginController::onSessionExpired()
{
    // Only a session we thought was good needs checking. Anywhere else a
    // check is already under way, or nobody is signed in — which is how the
    // two sources of this signal (the fetch that failed, and the WebView
    // transport noticing first) collapse into one check.
    if (m_phase != Phase::SignedIn)
        return;
    m_state = m_store.markExpiry();
    if (m_renewed.isValid() && m_renewed.elapsed() < RENEWAL_COOLDOWN_MS) {
        qCInfo(lcLogin) << "login: the session a silent check renewed was refused at once; asking instead";
        m_renewed.invalidate();
        m_clearCookiesFirst = true;
        setStatus(QStringLiteral("Your session ended"));
        setPhase(Phase::NeedsSignIn);
        return;
    }
    beginCheck();
}

void LoginController::confirmSignedIn()
{
    // The fetch proves whatever renewed the session worked.
    m_renewed.invalidate();
    if (m_phase != Phase::Welcome && m_phase != Phase::NeedsSignIn && m_phase != Phase::Checking)
        return;
    const bool first = !m_state.hasLoggedIn();
    setStatus(QString());
    setPhase(Phase::SignedIn);
    if (first) {
        m_state = m_store.markLogin();
        emit sessionChanged();
    }
}

void LoginController::checkAgain()
{
    if (m_phase == Phase::NeedsSignIn)
        beginCheck();
}

void LoginController::signOut()
{
    if (m_phase == Phase::Preview) {
        exitPreview();
        return;
    }

    clearCookies();
    m_store.clear();
    m_state = m_store.load();
    m_forgottenSession = false;
    m_clearCookiesFirst = false;
    m_renewed.invalidate();
    emit sessionChanged();

    setStatus(QString());
    setPhase(Phase::Welcome);
    qCInfo(lcLogin) << "signed out";
    emit signedOut();
    // Off whatever Self-Service page the surface was on, signed in.
    if (!m_demo)
        emit navigateRequested(QStringLiteral("about:blank"));
}

} // namespace mycu
