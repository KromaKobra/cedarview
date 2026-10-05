// The sync coordinator: what gets fetched when, against a recording fixture
// transport, with the real viewmodels and real worker threads (hence the
// QTRY_ waits).

#include "testsupport.h"

#include "core/providers/chapel.h"
#include "core/providers/chapel_schedule.h"
#include "core/providers/dining.h"
#include "core/providers/meals.h"
#include "platform/backend.h"
#include "ui/login.h"
#include "ui/modetransport.h"
#include "ui/sync.h"
#include "ui/viewmodels/chapel.h"
#include "ui/viewmodels/dining.h"

#include <QUuid>

#include <mutex>

using namespace mycu;

namespace {

const QString CHAPEL_URL = BASE_URL + QStringLiteral("/cedarinfo/chapelskip");

class NoBackend : public WebBackend
{
public:
    QString name() const override { return QStringLiteral("none"); }
    QString surfaceQml() const override { return QStringLiteral("WebSurfaceStub.qml"); }
    void clearCookies() override {}
};

// Serves the fixtures, counts what was asked for, and can pretend the session
// has ended.
class Recording : public Transport
{
public:
    Response get(const QString &path) override
    {
        {
            const std::lock_guard lock(mutex);
            seen.append(path);
        }
        if (expired && isSelfservice(resolve(path)))
            throw SessionExpired(QStringLiteral("gone"));
        if (offline)
            throw TransportError(QStringLiteral("could not reach ") + path);
        return inner.get(path);
    }

    int count(const QString &fragment)
    {
        const std::lock_guard lock(mutex);
        return static_cast<int>(
            std::count_if(seen.cbegin(), seen.cend(), [&](const QString &p) { return p.contains(fragment); }));
    }

    std::mutex mutex;
    QStringList seen;
    std::atomic<bool> expired{false};
    std::atomic<bool> offline{false};
    FixtureTransport inner{testing::fixturesDir()};
};

// One app's worth of parts, over one state directory.
struct App
{
    explicit App(const QString &dir, bool native = false, bool signedInBefore = false)
        : storage(Storage::at(dir))
    {
        if (signedInBefore)
            storage.session.markLogin();
        live = std::make_shared<Recording>();
        preview = std::make_shared<Recording>();
        mode = std::make_shared<ModeTransport>(live, preview);
        login = std::make_unique<LoginController>(storage.session, &backend);
        login->setTimings(40, 2000);
        chapel = std::make_unique<ChapelViewModel>(mode, storage);
        dining = std::make_unique<DiningViewModel>(mode, storage);
        sync = std::make_unique<SyncCoordinator>(SyncCoordinator::Parts{
            login.get(), chapel.get(), dining.get(), nullptr, nullptr, nullptr, nullptr, mode,
            storage.cache, native});
        QObject::connect(login.get(), &LoginController::navigateRequested,
                         [this](const QString &url) { navigations.append(url); });
    }

    void start(bool native = false)
    {
        login->start("/cedarinfo/chapelskip", native);
        sync->start();
    }

    Storage storage;
    NoBackend backend;
    std::shared_ptr<Recording> live;
    std::shared_ptr<Recording> preview;
    std::shared_ptr<ModeTransport> mode;
    std::unique_ptr<LoginController> login;
    std::unique_ptr<ChapelViewModel> chapel;
    std::unique_ptr<DiningViewModel> dining;
    std::unique_ptr<SyncCoordinator> sync;
    QStringList navigations;
};

} // namespace

class TestSync : public QObject
{
    Q_OBJECT

    QTemporaryDir m_root;
    QString freshDir() { return m_root.path() + "/" + QUuid::createUuid().toString(QUuid::Id128); }

    void waitIdle(App &app) { QTRY_VERIFY_WITH_TIMEOUT(!app.sync->busy(), 5000); }

    // Anything personal asked of Self-Service: the chapel pages or meals.
    static int personalRequests(App &app) { return app.live->count("CedarInfo") + app.live->count(MEALS_PATH); }

    // What v0.3 left behind when it lost its record of the sign-in.
    static void writeV03Session(const QString &dir)
    {
        const SessionStore store(dir);
        store.ensureDirs();
        QFile file(store.path());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"schema": 1, "last_success": 123.0})");
    }

private slots:
    void cleanup() { QThreadPool::globalInstance()->waitForDone(5000); }

    // ---- Startup ---------------------------------------------------------------

    void aFirstRunOnTheDesktopFetchesOnlyThePublicSources()
    {
        App app(freshDir());
        app.start();
        waitIdle(app);
        QCOMPARE(app.login->phase(), LoginController::Phase::Welcome);
        QVERIFY(app.live->count("diningdata") >= 1);
        QVERIFY(app.live->count("mediaserve") >= 1);
        QCOMPARE(app.live->count("CedarInfo"), 0);
        QVERIFY(app.dining->menuStatus()->hasData());
        // No session: the personal sources say so.
        QVERIFY(app.chapel->skipsStatus()->needsSignIn());
    }

    // Android after an upgrade from v0.3, which lost its record of the
    // sign-in: a quiet personal fetch, whose success skips Welcome.
    void anAndroidUpgradeFindsTheSessionTheWebViewKept()
    {
        const QString dir = freshDir();
        writeV03Session(dir);
        App app(dir, true);
        app.start(true);
        QTRY_COMPARE(app.login->phase(), LoginController::Phase::SignedIn);
        waitIdle(app);
        QCOMPARE(app.chapel->remaining(), 16);
        QVERIFY(app.login->hasLoggedInBefore());
    }

    void anAndroidUpgradeWithNoSessionStaysOnWelcome()
    {
        const QString dir = freshDir();
        writeV03Session(dir);
        App app(dir, true);
        app.live->expired = true;
        app.start(true);
        waitIdle(app);
        QCOMPARE(app.login->phase(), LoginController::Phase::Welcome);
        QVERIFY(app.navigations.isEmpty()); // no check: there was nothing to renew
        QCOMPARE(app.chapel->skipsStatus()->error(), QString());
    }

    // A fresh install has no session to find, and asks Self-Service nothing.
    void aFreshAndroidInstallLooksForNoSession()
    {
        App app(freshDir(), true);
        app.start(true);
        waitIdle(app);
        QCOMPARE(app.login->phase(), LoginController::Phase::Welcome);
        QCOMPARE(personalRequests(app), 0);
        QVERIFY(app.chapel->skipsStatus()->needsSignIn());
    }

    // Android, signed in before: straight to the personal fetch, no page.
    void aKnownSessionOnAndroidFetchesStraightAway()
    {
        App app(freshDir(), true, true);
        app.start(true);
        waitIdle(app);
        QCOMPARE(app.login->phase(), LoginController::Phase::SignedIn);
        QVERIFY(app.navigations.isEmpty());
        QCOMPARE(app.live->count(SUMMARY_PATH), 1);
        QCOMPARE(app.dining->mealsRemaining(), 16);
    }

    // The desktop must load a page before it can fetch: check, then fetch.
    void aKnownSessionOnTheDesktopWaitsForTheCheck()
    {
        App app(freshDir(), false, true);
        app.start();
        QCOMPARE(app.login->phase(), LoginController::Phase::Checking);
        QVERIFY(app.chapel->skipsStatus()->loading()); // awaiting the check
        QTRY_VERIFY(app.dining->menuStatus()->hasData() && !app.dining->menuStatus()->fetching());
        QCOMPARE(app.live->count("CedarInfo"), 0);

        app.login->onUrlChanged(CHAPEL_URL); // the address asked for: no fetch yet
        QCOMPARE(app.login->phase(), LoginController::Phase::Checking);
        app.login->onPageLoaded(CHAPEL_URL);
        waitIdle(app);
        QVERIFY(app.live->count(SUMMARY_PATH) >= 1);
        QCOMPARE(app.chapel->remaining(), 16);
    }

    // Fresh saved data is not fetched again at launch.
    void freshSavedDataIsNotFetchedAgain()
    {
        const QString dir = freshDir();
        {
            App first(dir, true, true);
            first.start(true);
            waitIdle(first);
        }
        App second(dir, true, true);
        QVERIFY(second.dining->menuStatus()->fromCache());
        const bool savedToday = second.dining->hasMenuFor(QDate::currentDate());
        second.start(true);
        waitIdle(second);
        QCOMPARE(second.live->count("mediaserve"), 0);
        QCOMPARE(second.live->count(SUMMARY_PATH), 0);
        // …though the menu still is when it does not have today in it (the
        // fixture is two days in 2026), once.
        QCOMPARE(second.live->count("diningdata"), savedToday ? 0 : 1);
    }

    // ---- An expired session ----------------------------------------------------

    // Both personal sources fail at once: one silent check, not two.
    void twoExpiriesAreOneCheck()
    {
        App app(freshDir(), true, true);
        app.live->expired = true;
        app.start(true);
        QTRY_COMPARE(app.login->phase(), LoginController::Phase::Checking);
        QTRY_VERIFY(!app.chapel->busy() && !app.dining->planStatus()->fetching());
        QCOMPARE(app.navigations, QStringList{CHAPEL_URL});

        // Single sign-on brings it back: fetched again.
        app.live->expired = false;
        app.login->onPageLoaded(CHAPEL_URL);
        waitIdle(app);
        QCOMPARE(app.chapel->remaining(), 16);
    }

    // Regression, found on the phone: a "renewed" session the fetches still
    // turn away. One silent check, then the banner — not the same check
    // again, out of sight, for as long as the app is open.
    void aRenewalTheFetchesRefuseEndsInTheBanner()
    {
        App app(freshDir(), true, true);
        app.live->expired = true;
        app.start(true);
        QTRY_COMPARE(app.login->phase(), LoginController::Phase::Checking);
        QTRY_VERIFY(!app.chapel->busy() && !app.dining->planStatus()->fetching());

        app.login->onPageLoaded(CHAPEL_URL); // the check says it is back; it is not
        QTRY_COMPARE(app.login->phase(), LoginController::Phase::NeedsSignIn);
        waitIdle(app);
        QCOMPARE(app.navigations, QStringList{CHAPEL_URL});
        QCOMPARE(app.login->phase(), LoginController::Phase::NeedsSignIn);
        QVERIFY(app.chapel->skipsStatus()->needsSignIn());
    }

    void pullingDownUnderTheBannerChecksAgain()
    {
        App app(freshDir(), false, true);
        app.start();
        app.login->onUrlChanged(QStringLiteral("https://login.microsoftonline.com/x/saml2"));
        QTRY_COMPARE(app.login->phase(), LoginController::Phase::NeedsSignIn);
        QVERIFY(app.chapel->skipsStatus()->needsSignIn());
        app.sync->refreshAll();
        QCOMPARE(app.login->phase(), LoginController::Phase::Checking);
        QCOMPARE(app.navigations.size(), 2);
    }

    // ---- Offline ---------------------------------------------------------------

    void everyFailureBeingTheNetworkIsOffline()
    {
        App app(freshDir(), true, true);
        app.live->offline = true;
        app.start(true);
        waitIdle(app);
        QVERIFY(app.sync->offline());
        app.live->offline = false;
        app.sync->refreshAll();
        waitIdle(app);
        QVERIFY(!app.sync->offline());
        QVERIFY(app.sync->lastUpdatedText().startsWith("updated "));
    }

    // ---- Sign-out and preview ----------------------------------------------------

    void signOutDeletesThePersonalRecords()
    {
        const QString dir = freshDir();
        App app(dir, true, true);
        app.start(true);
        waitIdle(app);
        QVERIFY(app.storage.cache.load(cachekey::CHAPEL));

        app.login->signOut();

        QCOMPARE(app.login->phase(), LoginController::Phase::Welcome);
        QVERIFY(app.chapel->skipsStatus()->needsSignIn());
        QVERIFY(!app.storage.cache.load(cachekey::CHAPEL));
        QVERIFY(!app.storage.cache.load(cachekey::MEALS));
        QVERIFY(app.storage.cache.load(cachekey::MENUS));
        QCOMPARE(app.chapel->remaining(), -1);
        QCOMPARE(app.dining->mealsRemaining(), -1);
        QCOMPARE(SessionStore(dir).load().studentId, QString());
    }

    // Regression, found on the phone: signed out, the app must stay signed
    // out — no quiet fetch at the next launch that could sign it back in.
    void anAndroidLaunchAfterSigningOutLooksForNoSession()
    {
        const QString dir = freshDir();
        writeV03Session(dir);
        {
            App first(dir, true);
            first.start(true);
            QTRY_COMPARE(first.login->phase(), LoginController::Phase::SignedIn);
            waitIdle(first);
            first.login->signOut();
            waitIdle(first);
        }
        App second(dir, true);
        second.start(true);
        waitIdle(second);
        QCOMPARE(second.login->phase(), LoginController::Phase::Welcome);
        QCOMPARE(personalRequests(second), 0);
    }

    // Sample data comes from the other transport, is never saved, and leaving
    // it brings back the user's own.
    void previewNeverTouchesTheUsersData()
    {
        const QString dir = freshDir();
        App app(dir, true, true);
        app.start(true);
        waitIdle(app);
        const auto saved = app.storage.cache.load(cachekey::CHAPEL);
        QVERIFY(saved);
        const int liveRequests = app.live->count("");

        app.login->startPreview();
        waitIdle(app);
        QVERIFY(app.preview->count(SUMMARY_PATH) >= 1);
        QCOMPARE(app.live->count(""), liveRequests);
        QCOMPARE(app.chapel->remaining(), 16);
        QCOMPARE(app.storage.cache.load(cachekey::CHAPEL)->savedAt, saved->savedAt);

        app.login->exitPreview();
        QCOMPARE(app.login->phase(), LoginController::Phase::SignedIn);
        QVERIFY(app.chapel->skipsStatus()->fromCache());
        QCOMPARE(app.chapel->remaining(), 16);
    }
};

CEDARVIEW_TEST_MAIN(TestSync, QCoreApplication)
#include "tst_sync.moc"
