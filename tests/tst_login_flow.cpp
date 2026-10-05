// The login state machine, driven by URLs alone.
//
// This is the whole of the auth design, and it is testable without a browser
// precisely because its only inputs are "which URL is the surface on" and
// "which page finished loading". No cookie inspection, nothing Entra can
// change under us. The one clock in it — the grace a silent check gets on
// Microsoft's page — is shortened here.
//
// The surfaces report the address they are sent to before anything has
// loaded, so a test that feeds onUrlChanged(CHAPEL) right after a navigation
// is replaying that echo, not a landing; a landing is onPageLoaded.

#include "testsupport.h"

#include "platform/backend.h"
#include "ui/login.h"

#include <QSignalSpy>

using namespace mycu;

namespace {

const QString CHAPEL = BASE_URL + QStringLiteral("/cedarinfo/chapelskip");
const QString IDP = QStringLiteral(
    "https://login.microsoftonline.com/81c32413-015d-4ba8-a93b-e1c28e355738"
    "/saml2?SAMLRequest=abc&RelayState=%2Fcedarinfo%2Fchapelskip");

class FakeBackend : public WebBackend
{
public:
    QString name() const override { return QStringLiteral("fake"); }
    QString surfaceQml() const override { return QStringLiteral("WebSurfaceStub.qml"); }
    void clearCookies() override
    {
        ++cleared;
        if (stubborn)
            throw std::runtime_error("no cookie API on this platform");
    }
    int cleared = 0;
    bool stubborn = false;
};

// Collects every signal the controller emits, in order.
struct Recorder
{
    explicit Recorder(LoginController &controller)
    {
        QObject::connect(&controller, &LoginController::navigateRequested,
                         [this](const QString &url) { navigations.append(url); });
        QObject::connect(&controller, &LoginController::loggedIn, [this] { events.append("loggedIn"); });
        QObject::connect(&controller, &LoginController::signedOut, [this] { events.append("signedOut"); });
        QObject::connect(&controller, &LoginController::previewChanged,
                         [this](bool on) { events.append(on ? "preview:on" : "preview:off"); });
    }
    QStringList navigations;
    QStringList events;
};

} // namespace

class TestLoginFlow : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    FakeBackend m_backend;
    std::unique_ptr<LoginController> m_controller;
    std::unique_ptr<Recorder> m_rec;

    // A device that has signed in before.
    void signedInBefore() { SessionStore(m_dir->path()).markLogin(); }

    // Microsoft's page, reached and loaded.
    void onMicrosoft()
    {
        m_controller->onUrlChanged(IDP);
        m_controller->onPageLoaded(IDP);
    }

    // A sign-in on Microsoft's page, back to where it started: the address
    // first (Android's probe, the desktop's urlChanged), then the page.
    void signInOnMicrosoft()
    {
        onMicrosoft();
        m_controller->onUrlChanged(CHAPEL);
        m_controller->onPageLoaded(CHAPEL);
    }

    void writeSessionFile(const QByteArray &json)
    {
        const SessionStore store(m_dir->path());
        store.ensureDirs();
        QFile file(store.path());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(json);
    }

    void rebuild(bool demo = false)
    {
        m_rec.reset();
        m_controller = std::make_unique<LoginController>(SessionStore(m_dir->path()), &m_backend, demo);
        m_controller->setTimings(40, 2000);
        m_rec = std::make_unique<Recorder>(*m_controller);
    }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_backend = FakeBackend();
        rebuild();
    }

    void cleanup()
    {
        m_rec.reset();
        m_controller.reset();
    }

    // ---- Starting --------------------------------------------------------------

    // CedarView's own screen first; Microsoft only when asked for.
    void aFirstRunOpensOnWelcomeAndLoadsNothing()
    {
        m_controller->start("/cedarinfo/chapelskip", false);
        QCOMPARE(m_controller->phase(), LoginController::Phase::Welcome);
        QCOMPARE(m_controller->phaseName(), QStringLiteral("welcome"));
        QVERIFY(m_rec->navigations.isEmpty());
        QCOMPARE(m_controller->surfaceVisible(), false);
    }

    // RelayState carries the return path, so there is no separate login step.
    void aPastSessionIsCheckedSilently()
    {
        signedInBefore();
        rebuild();
        m_controller->start("/cedarinfo/chapelskip", false);
        QCOMPARE(m_controller->phase(), LoginController::Phase::Checking);
        QCOMPARE(m_rec->navigations, QStringList{CHAPEL});
        QCOMPARE(m_controller->surfaceVisible(), false);
    }

    void aLiveSessionNeverShowsTheBrowser()
    {
        signedInBefore();
        rebuild();
        m_controller->start("/cedarinfo/chapelskip", false);
        m_controller->onUrlChanged(CHAPEL); // the address asked for, at once
        QCOMPARE(m_controller->phase(), LoginController::Phase::Checking);
        m_controller->onPageLoaded(CHAPEL);
        QCOMPARE(m_controller->phase(), LoginController::Phase::SignedIn);
        QCOMPARE(m_controller->surfaceVisible(), false);
        QCOMPARE(m_rec->events, QStringList{"loggedIn"});
    }

    // Entra single sign-on bounces through Microsoft and back on its own: no
    // flash of Microsoft's page, no banner.
    void singleSignOnWithinTheGraceIsInvisible()
    {
        signedInBefore();
        rebuild();
        m_controller->start("/cedarinfo/chapelskip", false);
        onMicrosoft();
        QCOMPARE(m_controller->phase(), LoginController::Phase::Checking);
        QCOMPARE(m_controller->surfaceVisible(), false);
        m_controller->onUrlChanged(CHAPEL);
        m_controller->onPageLoaded(CHAPEL);
        QCOMPARE(m_controller->phase(), LoginController::Phase::SignedIn);
        QCOMPARE(m_rec->events, QStringList{"loggedIn"});
        QTest::qWait(80); // past the grace: nothing more happens
        QCOMPARE(m_controller->phase(), LoginController::Phase::SignedIn);
    }

    // Still on Microsoft after the grace: the saved figures stay up, under a
    // banner, and Microsoft's page stays hidden until asked for.
    void aCheckThatStaysOnMicrosoftNeedsSignIn()
    {
        signedInBefore();
        rebuild();
        m_controller->start("/cedarinfo/chapelskip", false);
        m_controller->onUrlChanged(IDP);
        QTRY_COMPARE(m_controller->phase(), LoginController::Phase::NeedsSignIn);
        QCOMPARE(m_controller->surfaceVisible(), false);
        QVERIFY(m_rec->events.isEmpty());

        m_controller->startSignIn();
        QCOMPARE(m_controller->phase(), LoginController::Phase::SigningIn);
        QCOMPARE(m_controller->surfaceVisible(), true);
        onMicrosoft();
        m_controller->onUrlChanged(CHAPEL);
        QCOMPARE(m_controller->phase(), LoginController::Phase::SignedIn);
        QCOMPARE(m_controller->surfaceVisible(), false);
        QCOMPARE(m_rec->events, QStringList{"loggedIn"});
    }

    // Neither Microsoft nor Self-Service answers (offline): stop waiting and
    // let the fetches say what is wrong.
    void aCheckWithNoAnswerGivesUpWaiting()
    {
        signedInBefore();
        rebuild();
        m_controller->setTimings(40, 40);
        m_controller->start("/cedarinfo/chapelskip", false);
        QTRY_COMPARE(m_controller->phase(), LoginController::Phase::SignedIn);
        QVERIFY(!m_rec->events.contains("loggedIn"));
    }

    // Android fetches without a page: the first fetch is the check.
    void aNativeSessionStartsSignedInWithoutLoadingAnything()
    {
        signedInBefore();
        rebuild();
        m_controller->start("/cedarinfo/chapelskip", true);
        QCOMPARE(m_controller->phase(), LoginController::Phase::SignedIn);
        QVERIFY(m_rec->navigations.isEmpty());
    }

    // ---- Signing in ------------------------------------------------------------

    void signingInFromWelcome()
    {
        m_controller->start("/cedarinfo/chapelskip", false);
        m_controller->startSignIn();
        QCOMPARE(m_controller->phase(), LoginController::Phase::SigningIn);
        QCOMPARE(m_controller->surfaceVisible(), true);
        QCOMPARE(m_rec->navigations, QStringList{CHAPEL});
        QVERIFY(m_controller->status().contains("Sign in"));

        onMicrosoft();
        // Back from Microsoft: done at the address, before the page loads.
        m_controller->onUrlChanged(CHAPEL);
        QCOMPARE(m_controller->phase(), LoginController::Phase::SignedIn);
        QCOMPARE(m_controller->hasLoggedInBefore(), true);
        QCOMPARE(m_rec->events, QStringList{"loggedIn"});
    }

    // Regression, found on the phone: every sign-in "finished" at once and
    // then failed as an expired session. The surface reports the address it
    // is sent to before Self-Service has redirected it to Microsoft, and any
    // Self-Service address used to count as signed in.
    void theAddressAskedForIsNotASignIn()
    {
        m_controller->start("/cedarinfo/chapelskip", false);
        m_controller->startSignIn();
        m_controller->onUrlChanged(CHAPEL);
        QCOMPARE(m_controller->phase(), LoginController::Phase::SigningIn);
        QCOMPARE(m_controller->surfaceVisible(), true);
        QCOMPARE(m_controller->hasLoggedInBefore(), false);
        QVERIFY(m_rec->events.isEmpty());

        onMicrosoft();
        m_controller->onUrlChanged(CHAPEL);
        QCOMPARE(m_controller->phase(), LoginController::Phase::SignedIn);
    }

    // From the "session ended" banner the surface is still on the Microsoft
    // page the check gave up on, and Android's, once shown, reports that page
    // first. That is not this sign-in reaching Microsoft.
    void aSignInFromTheBannerIgnoresTheMicrosoftPageItWasLeftOn()
    {
        signedInBefore();
        rebuild();
        m_controller->start("/cedarinfo/chapelskip", false);
        onMicrosoft();
        QTRY_COMPARE(m_controller->phase(), LoginController::Phase::NeedsSignIn);

        m_controller->startSignIn();
        m_controller->onUrlChanged(IDP);    // the page it was left on
        m_controller->onUrlChanged(CHAPEL); // the address asked for
        QCOMPARE(m_controller->phase(), LoginController::Phase::SigningIn);
        QVERIFY(m_rec->events.isEmpty());

        signInOnMicrosoft();
        QCOMPARE(m_controller->phase(), LoginController::Phase::SignedIn);
        QCOMPARE(m_rec->events, QStringList{"loggedIn"});
    }

    // Signing in on a device whose session is in fact still good: Self-Service
    // loads without Microsoft, and that is the sign-in done.
    void aSignInWithALiveSessionEndsWhenThePageLoads()
    {
        m_controller->start("/cedarinfo/chapelskip", false);
        m_controller->startSignIn();
        m_controller->onUrlChanged(CHAPEL);
        m_controller->onPageLoaded(CHAPEL);
        QCOMPARE(m_controller->phase(), LoginController::Phase::SignedIn);
        QCOMPARE(m_controller->hasLoggedInBefore(), true);
    }

    // Entra bounces through several URLs; that is one sign-in, not five.
    void intermediateIdpHopsChangeNothing()
    {
        m_controller->start("/cedarinfo/chapelskip", false);
        m_controller->startSignIn();
        for (const QString &url : {IDP, IDP + "&sso_reload=true",
                                   QStringLiteral("https://login.microsoftonline.com/common/DeviceAuth")}) {
            m_controller->onUrlChanged(url);
            m_controller->onPageLoaded(url);
        }
        QCOMPARE(m_controller->phase(), LoginController::Phase::SigningIn);
        QVERIFY(m_rec->events.isEmpty());
    }

    void cancellingGoesBackToWhereItCameFrom()
    {
        m_controller->start("/cedarinfo/chapelskip", false);
        m_controller->startSignIn();
        m_controller->cancelSignIn();
        QCOMPARE(m_controller->phase(), LoginController::Phase::Welcome);

        signedInBefore();
        rebuild();
        m_controller->start("/cedarinfo/chapelskip", true);
        m_controller->onSessionExpired();
        onMicrosoft();
        QTRY_COMPARE(m_controller->phase(), LoginController::Phase::NeedsSignIn);
        m_controller->startSignIn();
        m_controller->cancelSignIn();
        QCOMPARE(m_controller->phase(), LoginController::Phase::NeedsSignIn);
    }

    void anEmptyUrlIsIgnored()
    {
        m_controller->onUrlChanged(QString());
        m_controller->onPageLoaded(QString());
        QVERIFY(m_rec->events.isEmpty());
    }

    void theLoginSurvivesARestart()
    {
        m_controller->start("/cedarinfo/chapelskip", false);
        m_controller->startSignIn();
        signInOnMicrosoft();

        LoginController second(SessionStore(m_dir->path()), &m_backend);
        QCOMPARE(second.hasLoggedInBefore(), true);
    }

    // ---- An expired session ----------------------------------------------------

    // The remedy for an expired session is a silent check first.
    void expiryWhileSignedInChecksSilently()
    {
        signedInBefore();
        rebuild();
        m_controller->start("/cedarinfo/chapelskip", true);
        m_controller->onSessionExpired();
        QCOMPARE(m_controller->phase(), LoginController::Phase::Checking);
        QCOMPARE(m_rec->navigations, QStringList{CHAPEL});
        QCOMPARE(m_controller->surfaceVisible(), false);

        // The other source noticing too is the same check, not a second one.
        m_controller->onSessionExpired();
        QCOMPARE(m_rec->navigations.size(), 1);

        m_controller->onPageLoaded(CHAPEL);
        QCOMPARE(m_controller->phase(), LoginController::Phase::SignedIn);
        QCOMPARE(m_rec->events, QStringList{"loggedIn"});
    }

    // A silent check that says the session is back, followed at once by a
    // fetch that says it is not, would otherwise go round for as long as the
    // app is open. Ask instead — and since the cookies the check renewed are
    // evidently no good, sign in without them.
    void aRenewalRefusedAtOnceAsksInsteadOfCheckingAgain()
    {
        signedInBefore();
        rebuild();
        m_controller->start("/cedarinfo/chapelskip", true);
        m_controller->onSessionExpired();
        m_controller->onPageLoaded(CHAPEL);
        QCOMPARE(m_controller->phase(), LoginController::Phase::SignedIn);

        m_controller->onSessionExpired();
        QCOMPARE(m_controller->phase(), LoginController::Phase::NeedsSignIn);
        QCOMPARE(m_rec->navigations.size(), 1);
        QCOMPARE(m_controller->status(), QStringLiteral("Your session ended"));

        QCOMPARE(m_backend.cleared, 0);
        m_controller->startSignIn();
        QCOMPARE(m_backend.cleared, 1);
        QCOMPARE(m_controller->phase(), LoginController::Phase::SigningIn);
        signInOnMicrosoft();
        m_controller->onSessionExpired();
        m_controller->startSignIn();
        QCOMPARE(m_backend.cleared, 1); // only the once
    }

    // A renewal a fetch has since proved is a session like any other.
    void aProvenRenewalIsCheckedSilentlyNextTime()
    {
        signedInBefore();
        rebuild();
        m_controller->start("/cedarinfo/chapelskip", true);
        m_controller->onSessionExpired();
        m_controller->onPageLoaded(CHAPEL);
        m_controller->confirmSignedIn();

        m_controller->onSessionExpired();
        QCOMPARE(m_controller->phase(), LoginController::Phase::Checking);
        QCOMPARE(m_rec->navigations.size(), 2);
    }

    // Straight after signing in, an expiry still gets its silent check: the
    // first fetch can race the cookie into the jar.
    void anExpiryRightAfterSigningInIsCheckedSilently()
    {
        m_controller->start("/cedarinfo/chapelskip", true);
        m_controller->startSignIn();
        signInOnMicrosoft();
        m_controller->onSessionExpired();
        QCOMPARE(m_controller->phase(), LoginController::Phase::Checking);
    }

    // With nobody signed in, there is nothing to renew — Android's first-run
    // probe failing, say.
    void expiryWithNoSessionIsNotAnEvent()
    {
        m_controller->start("/cedarinfo/chapelskip", true);
        m_controller->onSessionExpired();
        QCOMPARE(m_controller->phase(), LoginController::Phase::Welcome);
        QVERIFY(m_rec->navigations.isEmpty());
    }

    void checkingAgainFromTheBanner()
    {
        signedInBefore();
        rebuild();
        m_controller->start("/cedarinfo/chapelskip", false);
        onMicrosoft();
        QTRY_COMPARE(m_controller->phase(), LoginController::Phase::NeedsSignIn);
        m_controller->checkAgain();
        QCOMPARE(m_controller->phase(), LoginController::Phase::Checking);
        QCOMPARE(m_rec->navigations.size(), 2);
    }

    // A fetch that works proves the session — and on a first run, skips
    // Welcome (the WebView kept a session the app had forgotten).
    void aWorkingFetchConfirmsTheSession()
    {
        m_controller->start("/cedarinfo/chapelskip", true);
        m_controller->confirmSignedIn();
        QCOMPARE(m_controller->phase(), LoginController::Phase::SignedIn);
        QCOMPARE(m_controller->hasLoggedInBefore(), true);
    }

    // Only a device that last ran v0.3, which could lose its record of the
    // sign-in, may hold a session it does not know about.
    void onlyAnUpgradeFromV03MayHoldAForgottenSession()
    {
        QVERIFY(!m_controller->mayHoldForgottenSession()); // a fresh install

        writeSessionFile(R"({"schema": 1, "last_success": 123.0})");
        rebuild();
        QVERIFY(m_controller->mayHoldForgottenSession());
        m_controller->signOut();
        QVERIFY(!m_controller->mayHoldForgottenSession());
        rebuild();
        QVERIFY(!m_controller->mayHoldForgottenSession()); // signed out

        writeSessionFile(R"({"schema": 1, "last_login": 123.0})");
        rebuild();
        QVERIFY(!m_controller->mayHoldForgottenSession()); // it knows

        writeSessionFile(R"({"schema": 2, "last_success": 123.0})");
        rebuild();
        QVERIFY(!m_controller->mayHoldForgottenSession());
    }

    // ---- Sample data -----------------------------------------------------------

    void previewFromWelcomeAndBack()
    {
        m_controller->start("/cedarinfo/chapelskip", false);
        m_controller->startPreview();
        QCOMPARE(m_controller->phase(), LoginController::Phase::Preview);
        QVERIFY(m_controller->inPreview());

        m_controller->confirmSignedIn(); // a fixture "fetch" proves nothing
        QCOMPARE(m_controller->phase(), LoginController::Phase::Preview);

        m_controller->exitPreview();
        QCOMPARE(m_controller->phase(), LoginController::Phase::Welcome);
        QCOMPARE(m_rec->events, (QStringList{"preview:on", "preview:off"}));
    }

    // "Sample data · Sign in": out of preview and into the sign-in.
    void signingInFromPreview()
    {
        m_controller->start("/cedarinfo/chapelskip", false);
        m_controller->startPreview();
        m_controller->startSignIn();
        QCOMPARE(m_controller->phase(), LoginController::Phase::SigningIn);
        QCOMPARE(m_rec->events, (QStringList{"preview:on", "preview:off"}));
        QCOMPARE(m_rec->navigations, QStringList{CHAPEL});
    }

    void demoModeStartsInPreviewAndNeverTouchesTheNetwork()
    {
        rebuild(true);
        m_controller->start("/cedarinfo/chapelskip", false);
        QCOMPARE(m_controller->phase(), LoginController::Phase::Preview);
        m_controller->startSignIn();
        QCOMPARE(m_controller->phase(), LoginController::Phase::Preview);
        QVERIFY(m_rec->navigations.isEmpty());
        QVERIFY(m_controller->status().contains("--demo"));
    }

    // ---- Sign out --------------------------------------------------------------

    // Regression, found on the phone: sign-out sent the surface to Microsoft's
    // logout page, which (the cookies being gone already) could only bounce it
    // to Microsoft's sign-in page — on screen for up to fifteen seconds,
    // looking like being signed straight back in. It is local now, and done
    // at once.
    void signOutClearsEverythingAndIsBackAtWelcomeAtOnce()
    {
        m_controller->start("/cedarinfo/chapelskip", false);
        m_controller->startSignIn();
        signInOnMicrosoft();
        m_rec->navigations.clear();
        m_rec->events.clear();

        m_controller->signOut();

        QCOMPARE(m_backend.cleared, 1);
        QCOMPARE(m_controller->phase(), LoginController::Phase::Welcome);
        QCOMPARE(m_controller->surfaceVisible(), false);
        QCOMPARE(m_controller->hasLoggedInBefore(), false);
        QCOMPARE(m_rec->events, QStringList{"signedOut"});
        // Off the signed-in page, and nowhere near Microsoft.
        QCOMPARE(m_rec->navigations, QStringList{"about:blank"});

        // The next launch opens on Welcome.
        rebuild();
        m_controller->start("/cedarinfo/chapelskip", true);
        QCOMPARE(m_controller->phase(), LoginController::Phase::Welcome);
        QVERIFY(m_rec->navigations.isEmpty());
    }

    // After signing out, signing in is Microsoft's page and a real round trip
    // again — not the old session's address mistaken for one.
    void signingInAfterSigningOutGoesThroughMicrosoft()
    {
        m_controller->start("/cedarinfo/chapelskip", false);
        m_controller->startSignIn();
        signInOnMicrosoft();
        m_controller->signOut();

        m_controller->startSignIn();
        QCOMPARE(m_controller->phase(), LoginController::Phase::SigningIn);
        m_controller->onUrlChanged(CHAPEL);
        QCOMPARE(m_controller->phase(), LoginController::Phase::SigningIn);
        signInOnMicrosoft();
        QCOMPARE(m_controller->phase(), LoginController::Phase::SignedIn);
    }

    // Signing out of sample data is leaving it.
    void signOutOfPreviewLeavesIt()
    {
        m_controller->start("/cedarinfo/chapelskip", false);
        m_controller->startPreview();
        m_controller->signOut();
        QCOMPARE(m_controller->phase(), LoginController::Phase::Welcome);
        QCOMPARE(m_backend.cleared, 0);
    }

    // A backend that cannot clear cookies must not stop the sign-out.
    void signOutSurvivesABackendThatCannotClearCookies()
    {
        m_backend.stubborn = true;
        m_controller->signOut(); // must not throw
        QCOMPARE(m_controller->phase(), LoginController::Phase::Welcome);
        QVERIFY(m_rec->events.contains("signedOut"));
    }
};

CEDARVIEW_TEST_MAIN(TestLoginFlow, QCoreApplication)
#include "tst_login_flow.moc"
