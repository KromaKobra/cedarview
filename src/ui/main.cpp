// Application entry point: assembles everything and starts the event loop.
//
// Startup order is load-bearing and is the one thing in this file worth
// reading carefully:
//
// 1. backend->beforeApp() — the web engine must initialise **before**
//    QGuiApplication exists. For QtWebEngine getting this wrong crashes rather
//    than failing.
// 2. Create QGuiApplication.
// 3. backend->afterApp() — anything that needs the application but must come
//    before the QML engine loads.
// 4. Build the object graph. The viewmodels read back what the last run saved
//    as they are constructed, so the first frame already has figures in it.
// 5. Expose it to QML and load Main.qml.
// 6. Start the login flow, then the fetches (SyncCoordinator).
//
// Run it:
//
//     cedarview                      # real thing: sign in, fetch live data
//     cedarview --demo               # starts in sample-data preview; no network
//     cedarview --demo -v            # …with debug logging
//     cedarview --shoot shots/       # screenshots of every screen, then exit

#include "core/cache.h"
#include "core/httptransport.h"
#include "core/log.h"
#include "core/providers/chapel.h"
#include "core/providers/chapel_schedule.h"
#include "core/session.h"
#include "core/transport.h"
#include "platform/backend.h"
#include "ui/bridge.h"
#include "ui/login.h"
#include "ui/modetransport.h"
#include "ui/settings.h"
#include "ui/sync.h"
#include "ui/viewmodels/campus.h"
#include "ui/viewmodels/chapel.h"
#include "ui/viewmodels/curfew.h"
#include "ui/viewmodels/dining.h"
#include "ui/viewmodels/hours.h"
#include "ui/viewmodels/search.h"
#include "ui/viewmodels/today.h"
#include "ui/webviewtransport.h"

#ifdef Q_OS_ANDROID
#  include "platform/android_sessiontransport.h"
#  include "platform/android_urlprobe.h"
#else
#  include <QDir>
#  include <QQuickItem>
#  include <QQuickWindow>
#  include <QTemporaryDir>
#  include <QTimer>
#  include <condition_variable>
#  include <mutex>
#endif

#include <QCommandLineParser>
#include <QDirIterator>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStyleHints>
#include <QThreadPool>

#include <cstdio>
#include <memory>

using namespace mycu;

namespace {

struct Options
{
    bool demo = false;
    QString fixtures = QStringLiteral(":/fixtures");
    QString stateDir;
    bool verbose = false;
    QString shootDir;
    QDateTime clock;
};

// The command line, read before the application object exists — whether the
// web engine is initialised at all depends on --demo, and that has to be
// decided before QGuiApplication is constructed.
//
// On Android there is no command line worth the name: argv is whatever the
// launcher left there, and a stray flag must not stop the app from opening.
// It always runs the real, non-demo configuration.
Options parseOptions(int argc, char **argv)
{
    Options options;
#ifdef Q_OS_ANDROID
    Q_UNUSED(argc)
    Q_UNUSED(argv)
#else
    QStringList args;
    for (int i = 0; i < argc; ++i)
        args.append(QString::fromLocal8Bit(argv[i]));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Your Cedarville records."));
    const QCommandLineOption help = parser.addHelpOption();
    const QCommandLineOption version(QStringLiteral("version"), QStringLiteral("Show the version and exit."));
    const QCommandLineOption demo(QStringLiteral("demo"),
                                  QStringLiteral("Start in sample-data preview, with no network, no sign-in "
                                                 "and no browser. Use this to work on the UI."));
    const QCommandLineOption fixtures(QStringLiteral("fixtures"),
                                      QStringLiteral("Where sample data comes from (default: the copy built "
                                                     "into the app, %1).")
                                          .arg(options.fixtures),
                                      QStringLiteral("dir"));
    const QCommandLineOption stateDir(QStringLiteral("state-dir"),
                                      QStringLiteral("Override where session metadata, the saved data and the "
                                                     "cookie jar live."),
                                      QStringLiteral("dir"));
    const QCommandLineOption verbose({QStringLiteral("v"), QStringLiteral("verbose")},
                                     QStringLiteral("Debug logging."));
    const QCommandLineOption shoot(QStringLiteral("shoot"),
                                   QStringLiteral("Development: save a screenshot of every screen, in both "
                                                  "themes and two phone widths, into <dir>, then exit. Implies "
                                                  "--demo and a throwaway state directory."),
                                   QStringLiteral("dir"));
    const QCommandLineOption clock(QStringLiteral("clock"),
                                   QStringLiteral("Development: pretend it is this local time "
                                                  "(2026-09-17T09:42)."),
                                   QStringLiteral("when"));
    parser.addOptions({version, demo, fixtures, stateDir, verbose, shoot, clock});

    if (!parser.parse(args)) {
        std::fprintf(stderr, "%s\n\n%s", qPrintable(parser.errorText()), qPrintable(parser.helpText()));
        std::exit(2);
    }
    if (parser.isSet(help)) {
        std::printf("%s", qPrintable(parser.helpText()));
        std::exit(0);
    }
    if (parser.isSet(version)) {
        std::printf("CedarView %s\n", CEDARVIEW_VERSION);
        std::exit(0);
    }

    options.demo = parser.isSet(demo);
    if (parser.isSet(fixtures))
        options.fixtures = parser.value(fixtures);
    options.stateDir = parser.value(stateDir);
    options.verbose = parser.isSet(verbose);
    options.shootDir = parser.value(shoot);
    if (!options.shootDir.isEmpty())
        options.demo = true;
    if (parser.isSet(clock)) {
        options.clock = QDateTime::fromString(parser.value(clock), Qt::ISODate);
        if (!options.clock.isValid()) {
            std::fprintf(stderr, "--clock wants an ISO date and time, like 2026-09-17T09:42\n");
            std::exit(2);
        }
    }
#endif
    return options;
}

// The two typefaces, as static instances bundled with the QML (qml/fonts/,
// with their OFL licences). Registered before QML loads so Theme.qml's family
// names resolve on the first frame. A font that fails to load leaves the
// platform's own in its place — the text still renders.
void registerFonts()
{
    QDirIterator it(QStringLiteral(":/qt/qml/CedarView/fonts"), {QStringLiteral("*.ttf")});
    int loaded = 0;
    while (it.hasNext()) {
        if (QFontDatabase::addApplicationFont(it.next()) >= 0)
            ++loaded;
        else
            qCWarning(lcApp).noquote() << "could not load font" << it.filePath();
    }
    qCDebug(lcApp) << "fonts:" << loaded << "loaded";
}

#ifndef Q_OS_ANDROID
// --shoot only: holds the sample data back until released, so the first
// screenshots catch the loading state — skeletons — rather than racing it.
class PausedTransport : public Transport
{
public:
    explicit PausedTransport(TransportPtr inner) : m_inner(std::move(inner)) {}

    Response get(const QString &path) override
    {
        {
            std::unique_lock lock(m_mutex);
            m_released.wait(lock, [this] { return m_open; });
        }
        return m_inner->get(path);
    }

    void release()
    {
        {
            const std::lock_guard lock(m_mutex);
            m_open = true;
        }
        m_released.notify_all();
    }

private:
    TransportPtr m_inner;
    std::mutex m_mutex;
    std::condition_variable m_released;
    bool m_open = false;
};

// --shoot: every screen, in both themes and at two phone widths, saved as
// PNGs, then the app exits. Each shot is `<theme>-<width>-<screen>.png`. The
// screens are put up by Main.qml's showScreen(), so nothing here knows the
// QML's insides. Heights follow the canvas's boards, so a long tab is shot
// whole.
void shoot(QQmlApplicationEngine &engine, const QString &dir, SettingsController &settings,
           LoginController &login, SyncCoordinator &sync, const std::shared_ptr<QDateTime> &clock,
           const std::shared_ptr<PausedTransport> &paused)
{
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!window) {
        qCCritical(lcApp) << "--shoot: no window";
        QCoreApplication::exit(1);
        return;
    }
    QDir().mkpath(dir);

    struct Shot
    {
        QString screen;
        int height;
        QTime at; // a time of day for the clock, or null to leave it
    };
    const QList<Shot> shots = {
        {QStringLiteral("welcome"), 844, {}},
        {QStringLiteral("today"), 844, QTime(9, 42)},
        {QStringLiteral("today-night"), 844, QTime(22, 48)},
        {QStringLiteral("chapel"), 1600, QTime(9, 42)},
        {QStringLiteral("chapel-sheet"), 844, QTime(9, 42)},
        {QStringLiteral("plan"), 1250, QTime(9, 42)},
        {QStringLiteral("menu"), 1500, QTime(9, 42)},
        {QStringLiteral("hours"), 1300, QTime(9, 42)},
        {QStringLiteral("campus"), 1800, QTime(22, 48)},
        {QStringLiteral("search"), 844, QTime(9, 42)},
    };

    auto settle = [] {
        // Long enough for bindings, a layout pass and the fade-ins.
        QEventLoop loop;
        QTimer::singleShot(700, &loop, &QEventLoop::quit);
        loop.exec();
    };
    auto grab = [&](const QString &name) {
        const QString path = QDir(dir).filePath(name + QStringLiteral(".png"));
        if (!window->grabWindow().save(path))
            qCWarning(lcApp).noquote() << "--shoot: could not write" << path;
        else
            qCInfo(lcApp).noquote() << "--shoot:" << path;
    };
    auto show = [&](const QString &screen) {
        QMetaObject::invokeMethod(window, "showScreen", Q_ARG(QVariant, screen));
    };

    // Cold: nothing saved, nothing back yet — the skeletons.
    window->resize(360, 844);
    for (const bool light : {false, true}) {
        settings.setLightMode(light);
        show(QStringLiteral("today"));
        settle();
        grab(QStringLiteral("%1-360-cold-today").arg(light ? u"light" : u"dark"));
    }
    paused->release();
    settle();

    for (const bool light : {false, true}) {
        settings.setLightMode(light);
        for (const int width : {360, 412}) {
            for (const Shot &shot : shots) {
                if (shot.at.isValid()) {
                    *clock = QDateTime(clock->date(), shot.at);
                    sync.tick();
                }
                if (shot.screen == u"welcome")
                    login.exitPreview();
                else if (!login.inPreview())
                    login.startPreview();
                window->resize(width, shot.height);
                show(shot.screen);
                settle();
                grab(QStringLiteral("%1-%2-%3").arg(light ? u"light" : u"dark").arg(width).arg(shot.screen));
            }
        }
    }
    QCoreApplication::exit(0);
}
#endif

} // namespace

int main(int argc, char **argv)
{
    Options options = parseOptions(argc, argv);

#ifndef Q_OS_ANDROID
    // To the terminal, or to whatever the output is piped into — never quietly
    // to journald, which is where Qt sends it when stderr is not a tty. A log
    // that vanishes when you redirect it is a log nobody reads. (Android keeps
    // Qt's logcat handler.)
    if (!qEnvironmentVariableIsSet("QT_FORCE_STDERR_LOGGING"))
        qputenv("QT_FORCE_STDERR_LOGGING", "1");
    // Screenshots are taken of a throwaway profile, never the user's.
    std::unique_ptr<QTemporaryDir> shootState;
    if (!options.shootDir.isEmpty()) {
        shootState = std::make_unique<QTemporaryDir>();
        options.stateDir = shootState->path();
        if (!options.clock.isValid())
            options.clock = QDateTime(QDate(2026, 9, 17), QTime(9, 42));
    }
#endif

    // "INFO mycu.login: …", the shape the log has always had.
    qSetMessagePattern(QStringLiteral("%{if-debug}DEBUG%{endif}%{if-info}INFO%{endif}"
                                      "%{if-warning}WARNING%{endif}%{if-critical}ERROR%{endif}"
                                      "%{if-fatal}FATAL%{endif} %{category}: %{message}"));
    if (options.verbose)
        QLoggingCategory::setFilterRules(QStringLiteral("mycu*.debug=true"));

    std::unique_ptr<WebBackend> backend(createBackend());
    qCInfo(lcApp).noquote() << "CedarView" << CEDARVIEW_VERSION << "—" << backend->name();

    // --- 1. Web engine init that must precede the application object --------
    if (!options.demo)
        backend->beforeApp();

    // --- 2. The application -------------------------------------------------
    QGuiApplication app(argc, argv);
    // These two decide where QSettings writes (~/.config/Kroma/CedarView.conf
    // on Linux, app-private storage on Android). The login is not stored
    // there — see core/session.h, which keys off its own APP_DIR_NAME.
    QGuiApplication::setApplicationName(QStringLiteral("CedarView"));
    QGuiApplication::setOrganizationName(QStringLiteral("Kroma"));
    QGuiApplication::setApplicationVersion(QStringLiteral(CEDARVIEW_VERSION));

    // --- 3. Web engine init that must follow it -----------------------------
    if (!options.demo)
        backend->afterApp();
    registerFonts();

    // --- 4. Object graph ----------------------------------------------------
    SessionStore store(options.stateDir);
    store.ensureDirs();
    const PayloadCache cache(store.stateDir());
    const Storage storage{store, cache};

    // The clock everything reads. Real time, unless --clock (or --shoot) pins
    // it — then the sample data is redated around that day too.
    auto pinned = std::make_shared<QDateTime>(options.clock);
    const std::function<QDateTime()> clock = options.clock.isValid()
        ? std::function<QDateTime()>([pinned] { return *pinned; })
        : std::function<QDateTime()>([] { return QDateTime::currentDateTime(); });

    // The app talks to three services with different auth, so requests are
    // routed by origin (see TransportRouter):
    //
    //   selfservice.cedarville.edu   SAML/Entra session   -> the WebView (desktop),
    //                                                        HTTPS with its cookies (Android)
    //   diningdata.cedarville.edu    none at all          -> plain HTTPS
    //   mediaserve.cedarville.edu    none at all          -> plain HTTPS
    //
    // Routing is a correctness requirement, not a shortcut: an in-page fetch()
    // is bound by the same-origin policy, so a WebView parked on Self-Service
    // could not reach the dining API even if we wanted it to.
    //
    // Sample-data preview swaps all of it for the bundled fixtures at runtime
    // (ModeTransport). --demo has no live side at all.
    std::shared_ptr<WebViewTransport> sessionTransport;
    TransportPtr live;
    QString surfaceQml;
    bool nativeSession = false;
    if (options.demo) {
        live = std::make_shared<UnavailableTransport>();
        surfaceQml = QStringLiteral("WebSurfaceStub.qml");
        qCInfo(lcApp).noquote() << "demo mode: sample data from" << options.fixtures;
    } else {
        sessionTransport = std::make_shared<WebViewTransport>();
        // Both of these are public services with no session of their own, so
        // they go straight out over HTTPS rather than through the browser.
        auto router = std::make_shared<TransportRouter>(sessionTransport);
        router->route(DINING_BASE, std::make_shared<HttpTransport>())
            .route(CHAPEL_MEDIA_BASE, std::make_shared<HttpTransport>());
#ifdef Q_OS_ANDROID
        // Self-Service, too, goes over plain HTTPS on Android, with the
        // WebView's cookies. The WebView still signs you in, but no script
        // is run in it: QtWebView 6.11's runJavaScript runs its callback on
        // the wrong thread and crashes the app. See
        // platform/android_sessiontransport.h. It also means the personal
        // figures need no page loaded first, which is what lets a launch go
        // straight to fetching them.
        router->route(BASE_URL, std::make_shared<AndroidSessionTransport>());
        nativeSession = true;
#endif
        live = router;
        surfaceQml = backend->surfaceQml();
        backend->configureProfile(store.profileDir());
    }
    TransportPtr preview = makePreviewTransport(options.fixtures, [clock] { return clock().date(); });
#ifndef Q_OS_ANDROID
    std::shared_ptr<PausedTransport> paused;
    if (!options.shootDir.isEmpty()) {
        paused = std::make_shared<PausedTransport>(preview);
        preview = paused;
    }
#endif
    auto mode = std::make_shared<ModeTransport>(live, preview);

    LoginController login(store, backend.get(), options.demo);
    ChapelViewModel chapel(mode, storage);
    DiningViewModel dining(mode, storage);
    // No transport for these: curfew and opening hours are hand-entered
    // (core/curfew.h, core/hours.h), because no Cedarville service publishes
    // them.
    CurfewViewModel curfew;
    HoursViewModel hours;
    CampusViewModel campus;
    // Default QSettings, so it must be built after setApplicationName and
    // setOrganizationName above — otherwise it writes to a file named after
    // the executable.
    SettingsController settings;
    dining.attachSettings(&settings);
    campus.attachSettings(&settings);
    TodayViewModel today(&chapel, &dining, &curfew);
    SearchViewModel search(&chapel, &dining, &settings);

    chapel.now = clock;
    dining.now = clock;
    curfew.now = clock;
    hours.now = clock;
    campus.now = clock;
    today.now = clock;
    search.now = clock;

    SyncCoordinator sync({&login, &chapel, &dining, &curfew, &hours, &campus, &today, mode, cache, nativeSession});
    sync.now = clock;
    sync.tick();

    // The system bars follow the app's theme, not the phone's. Edge to edge
    // (forced from targetSdk 35) puts the status bar over the app's own
    // header, and Android picks light or dark status-bar icons from the colour
    // scheme Qt reports — so a dark app on a phone in light mode would
    // otherwise get dark icons on a dark header. Harmless on desktop, where
    // the app paints every colour itself anyway.
    auto applyColorScheme = [&] {
        QGuiApplication::styleHints()->setColorScheme(settings.lightMode() ? Qt::ColorScheme::Light
                                                                           : Qt::ColorScheme::Dark);
    };
    applyColorScheme();
    QObject::connect(&settings, &SettingsController::changed, &app, applyColorScheme);

    // Back in the foreground: whatever went stale while the app was away.
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &sync, [&sync](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive)
            sync.resume();
    });

    Bridge bridge(sessionTransport.get(), CHAPEL_PATH, backend->name(), surfaceQml);

    if (sessionTransport) {
        // The WebView transport also detects expiry directly, before the
        // exception has propagated back through the worker thread — the login
        // controller collapses the two into one check.
        QObject::connect(sessionTransport.get(), &WebViewTransport::sessionExpired, &login,
                         &LoginController::onSessionExpired);
    }

    // --- 5. QML -------------------------------------------------------------
    auto engine = std::make_unique<QQmlApplicationEngine>();
    QQmlContext *ctx = engine->rootContext();
    ctx->setContextProperty(QStringLiteral("bridge"), &bridge);
    ctx->setContextProperty(QStringLiteral("login"), &login);
    ctx->setContextProperty(QStringLiteral("sync"), &sync);
    ctx->setContextProperty(QStringLiteral("chapel"), &chapel);
    ctx->setContextProperty(QStringLiteral("dining"), &dining);
    ctx->setContextProperty(QStringLiteral("curfew"), &curfew);
    ctx->setContextProperty(QStringLiteral("hours"), &hours);
    ctx->setContextProperty(QStringLiteral("campus"), &campus);
    ctx->setContextProperty(QStringLiteral("today"), &today);
    ctx->setContextProperty(QStringLiteral("search"), &search);
    // Read by every Theme.qml instance, which is how many separate copies of
    // the palette agree on which one is showing.
    ctx->setContextProperty(QStringLiteral("settings"), &settings);
    ctx->setContextProperty(QStringLiteral("platformSurface"), surfaceQml);
    // Desktop only: the persistent profile WebSurfaceDesktop.qml binds to.
    ctx->setContextProperty(QStringLiteral("webProfile"), backend->qmlProfile());
#ifdef Q_OS_ANDROID
    // Android only: what WebSurfaceAndroid.qml polls for the address QtWebView
    // reports late. See platform/android_urlprobe.h.
    AndroidUrlProbe urlProbe;
    ctx->setContextProperty(QStringLiteral("urlProbe"), &urlProbe);
#endif

    engine->loadFromModule("CedarView", "Main");
    if (engine->rootObjects().isEmpty()) {
        qCCritical(lcApp) << "QML failed to load. On desktop this is almost always QtWebEngine "
                             "missing from QML2_IMPORT_PATH — are you in the nix devShell?";
        return 1;
    }

    // --- 6. Sign-in, then the fetches ---------------------------------------
    login.start(bridge.startPath(), nativeSession);
    sync.start();

#ifndef Q_OS_ANDROID
    if (!options.shootDir.isEmpty()) {
        QTimer::singleShot(0, &app, [&] { shoot(*engine, options.shootDir, settings, login, sync, pinned, paused); });
    }
#endif

    const int code = QGuiApplication::exec();

    // Tear down in dependency order: QtWebEngine crashes if the web profile
    // goes before the views using it. The surface goes with the engine, so
    // any request still waiting on it is failed, and the workers are given a
    // moment to finish before the objects they report to are destroyed.
    engine.reset();
    if (sessionTransport)
        sessionTransport->close();
    QThreadPool::globalInstance()->waitForDone(5000);
    backend->shutdown();
    return code;
}
