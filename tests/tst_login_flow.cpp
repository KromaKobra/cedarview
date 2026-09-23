// The login state machine, driven by URLs alone.
//
// This is the whole of the auth design, and it is testable without a browser
// precisely because its only input is "which URL is the surface on". No
// cookie inspection, no timers, nothing Entra can change under us.

#include "testsupport.h"

#include "platform/backend.h"
#include "ui/login.h"

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
        QObject::connect(&controller, &LoginController::loginNeeded, [this] { events.append("loginNeeded"); });
        QObject::connect(&controller, &LoginController::signedOut, [this] { events.append("signedOut"); });
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

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_backend = FakeBackend();
        m_controller = std::make_unique<LoginController>(SessionStore(m_dir->path()), &m_backend);
        m_rec = std::make_unique<Recorder>(*m_controller);
    }

    void cleanup()
    {
        m_rec.reset();
        m_controller.reset();
    }

    // RelayState carries the return path, so there is no separate login step.
    void beginNavigatesToTheTargetNotToALoginPage()
    {
        m_controller->begin("/cedarinfo/chapelskip");
        QCOMPARE(m_rec->navigations, QStringList{CHAPEL});
        QCOMPARE(m_controller->surfaceVisible(), false);
    }

    void aLiveSessionNeverShowsTheBrowser()
    {
        m_controller->begin("/cedarinfo/chapelskip");
        m_controller->onUrlChanged(CHAPEL);
        QCOMPARE(m_controller->surfaceVisible(), false);
        QCOMPARE(m_rec->events, QStringList{"loggedIn"});
    }

    void landingOnTheIdpRaisesTheSignInSurface()
    {
        m_controller->begin("/cedarinfo/chapelskip");
        m_controller->onUrlChanged(IDP);
        QCOMPARE(m_controller->surfaceVisible(), true);
        QCOMPARE(m_rec->events, QStringList{"loginNeeded"});
        QVERIFY(m_controller->status().contains("Sign in"));
    }

    void comingBackToSelfserviceCompletesTheLogin()
    {
        m_controller->begin("/cedarinfo/chapelskip");
        m_controller->onUrlChanged(IDP);
        m_controller->onUrlChanged(CHAPEL);
        QCOMPARE(m_controller->surfaceVisible(), false);
        QCOMPARE(m_rec->events, (QStringList{"loginNeeded", "loggedIn"}));
        QCOMPARE(m_controller->hasLoggedInBefore(), true);
    }

    // Entra bounces through several URLs; that is one sign-in, not five.
    void intermediateIdpHopsDoNotReSignal()
    {
        m_controller->begin("/cedarinfo/chapelskip");
        for (const QString &url : {IDP, IDP + "&sso_reload=true",
                                   QStringLiteral("https://login.microsoftonline.com/common/DeviceAuth")})
            m_controller->onUrlChanged(url);
        QCOMPARE(m_rec->events, QStringList{"loginNeeded"});
    }

    void anEmptyUrlIsIgnored()
    {
        m_controller->onUrlChanged(QString());
        QVERIFY(m_rec->events.isEmpty());
    }

    // The remedy for an expired session is the remedy for never having one.
    void expiryRestartsTheSameFlow()
    {
        m_controller->begin("/cedarinfo/chapelskip");
        m_controller->onUrlChanged(CHAPEL);
        m_rec->navigations.clear();

        m_controller->onSessionExpired();

        QCOMPARE(m_rec->navigations, QStringList{CHAPEL});
        QVERIFY(m_controller->status().contains("session ended"));
    }

    void theLoginSurvivesARestart()
    {
        FakeBackend backend;
        LoginController first(SessionStore(m_dir->path()), &backend);
        first.begin("/cedarinfo/chapelskip");
        first.onUrlChanged(IDP);
        first.onUrlChanged(CHAPEL);

        LoginController second(SessionStore(m_dir->path()), &backend);
        QCOMPARE(second.hasLoggedInBefore(), true);
    }

    // ---- Sign out ------------------------------------------------------------

    void signOutClearsCookiesAndGoesToTheFederatedLogout()
    {
        m_controller->begin("/cedarinfo/chapelskip");
        m_controller->onUrlChanged(CHAPEL);
        m_rec->navigations.clear();

        m_controller->signOut();

        QCOMPARE(m_backend.cleared, 1);
        QCOMPARE(m_rec->navigations, QStringList{LOGOUT_URL});
        QCOMPARE(m_controller->surfaceVisible(), true);
        QCOMPARE(m_controller->hasLoggedInBefore(), false);
    }

    // Not the moment we navigate — the logout URL contains 'post_logout'.
    void signOutOnlyCompletesOnceWeAreBackOnSelfservice()
    {
        m_controller->signOut();

        m_controller->onUrlChanged(LOGOUT_URL);
        QVERIFY(!m_rec->events.contains("signedOut"));
        QCOMPARE(m_controller->surfaceVisible(), true);

        m_controller->onUrlChanged(BASE_URL + "/");
        QVERIFY(m_rec->events.contains("signedOut"));
        QCOMPARE(m_controller->surfaceVisible(), false);
    }

    // Android has no cookie API; sign-out must still work there.
    void signOutSurvivesABackendThatCannotClearCookies()
    {
        m_backend.stubborn = true;
        m_controller->signOut(); // must not throw
        QCOMPARE(m_rec->navigations, QStringList{LOGOUT_URL});
    }

    // ---- Demo mode -----------------------------------------------------------

    void demoModeNeverTouchesTheNetwork()
    {
        FakeBackend backend;
        LoginController controller(SessionStore(m_dir->path()), &backend, true);
        Recorder rec(controller);

        controller.begin("/cedarinfo/chapelskip");

        QVERIFY(rec.navigations.isEmpty());
        QCOMPARE(controller.surfaceVisible(), false);
        QVERIFY(controller.status().contains("Demo"));
    }
};

CEDARVIEW_TEST_MAIN(TestLoginFlow, QCoreApplication)
#include "tst_login_flow.moc"
