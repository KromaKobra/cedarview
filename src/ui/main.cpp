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
// 4. Build the object graph, expose it to QML, load Main.qml.
//
// Run it:
//
//     cedarview                      # real thing: log in, fetch live data
//     cedarview --demo               # fixtures only; no network, no login
//     cedarview --demo -v            # …with debug logging

#include "core/httptransport.h"
#include "core/log.h"
#include "core/providers/chapel.h"
#include "core/providers/chapel_schedule.h"
#include "core/session.h"
#include "core/transport.h"
#include "platform/backend.h"
#include "ui/bridge.h"
#include "ui/demotransport.h"
#include "ui/login.h"
#include "ui/settings.h"
#include "ui/viewmodels/chapel.h"
#include "ui/viewmodels/dining.h"
#include "ui/viewmodels/semester.h"
#include "ui/webviewtransport.h"

#ifdef Q_OS_ANDROID
#  include "platform/android_sessiontransport.h"
#endif

#include <QCommandLineParser>
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
    QString fixtures = QStringLiteral(CEDARVIEW_FIXTURES_DIR);
    QString stateDir;
    bool verbose = false;
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
                                  QStringLiteral("Run against tests/fixtures/ instead of the network — "
                                                 "no login, no browser, no Cedarville. Use this to work "
                                                 "on the UI."));
    const QCommandLineOption fixtures(QStringLiteral("fixtures"),
                                      QStringLiteral("Fixture directory for --demo (default: %1).")
                                          .arg(options.fixtures),
                                      QStringLiteral("dir"));
    const QCommandLineOption stateDir(QStringLiteral("state-dir"),
                                      QStringLiteral("Override where session metadata and the cookie "
                                                     "jar live."),
                                      QStringLiteral("dir"));
    const QCommandLineOption verbose({QStringLiteral("v"), QStringLiteral("verbose")},
                                     QStringLiteral("Debug logging."));
    parser.addOptions({version, demo, fixtures, stateDir, verbose});

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
#endif
    return options;
}

} // namespace

int main(int argc, char **argv)
{
    const Options options = parseOptions(argc, argv);

#ifndef Q_OS_ANDROID
    // To the terminal, or to whatever the output is piped into — never quietly
    // to journald, which is where Qt sends it when stderr is not a tty. A log
    // that vanishes when you redirect it is a log nobody reads. (Android keeps
    // Qt's logcat handler.)
    if (!qEnvironmentVariableIsSet("QT_FORCE_STDERR_LOGGING"))
        qputenv("QT_FORCE_STDERR_LOGGING", "1");
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

    // --- 4. Object graph ----------------------------------------------------
    SessionStore store(options.stateDir);
    store.ensureDirs();

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
    std::shared_ptr<WebViewTransport> sessionTransport;
    TransportPtr transport;
    QString surfaceQml;
    if (options.demo) {
        auto fixtures = std::make_shared<FixtureTransport>(options.fixtures);
        // The captured menus are for two days in September 2026; this moves
        // them onto whatever dates are asked for, so Home Cooking has a menu
        // today and on any day paged to.
        auto router = std::make_shared<TransportRouter>(fixtures);
        router->route(DINING_BASE, std::make_shared<RedatedMenusTransport>(fixtures));
        transport = router;
        surfaceQml = QStringLiteral("WebSurfaceStub.qml");
        qCInfo(lcApp).noquote() << "demo mode: serving fixtures from" << options.fixtures;
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
        // platform/android_sessiontransport.h.
        router->route(BASE_URL, std::make_shared<AndroidSessionTransport>());
#endif
        transport = router;
        surfaceQml = backend->surfaceQml();
        backend->configureProfile(store.profileDir());
    }

    LoginController login(store, backend.get(), options.demo);
    ChapelViewModel chapel(transport, store);
    DiningViewModel dining(transport);
    // No transport: the term's dates are in core/calendar.h, because no
    // Cedarville service publishes them. See that file for the apology.
    SemesterViewModel semester;
    // Default QSettings, so it must be built after setApplicationName and
    // setOrganizationName above — otherwise it writes to a file named after
    // the executable.
    SettingsController settings;

    // The system bars follow the app's theme, not the phone's. Edge to edge
    // (forced from targetSdk 35) puts the status bar over the app's own
    // ribbon, and Android picks light or dark status-bar icons from the colour
    // scheme Qt reports — so a dark app on a phone in light mode would
    // otherwise get dark icons on a dark ribbon. Harmless on desktop, where
    // the app paints every colour itself anyway.
    auto applyColorScheme = [&] {
        QGuiApplication::styleHints()->setColorScheme(settings.lightMode() ? Qt::ColorScheme::Light
                                                                           : Qt::ColorScheme::Dark);
    };
    applyColorScheme();
    QObject::connect(&settings, &SettingsController::changed, &app, applyColorScheme);

    Bridge bridge(sessionTransport.get(), CHAPEL_PATH, backend->name(), surfaceQml);

    // The expiry loop, in two connections:
    //   a failed fetch  -> reopen the sign-in surface
    //   a completed login -> retry the fetch
    QObject::connect(&chapel, &ChapelViewModel::sessionExpired, &login, &LoginController::onSessionExpired);
    QObject::connect(&login, &LoginController::loggedIn, &chapel, &ChapelViewModel::refresh);
    QObject::connect(&login, &LoginController::loggedIn, &dining, &DiningViewModel::refreshPlan);
    QObject::connect(&login, &LoginController::signedOut, &app, [] { qCInfo(lcApp) << "signed out"; });

    if (!options.demo) {
        // Neither of these needs a session, so they do not wait for the login
        // flow — the menu and the next speaker are on screen while you sign in.
        dining.refresh();
        chapel.refreshSchedule();

        // The WebView transport also detects expiry directly, before the
        // exception has propagated back through the worker thread — connecting
        // both means the login surface appears as soon as we know, not a beat
        // later. Note this is the session transport, not the router: only the
        // session-bearing transport can have an expired session.
        QObject::connect(sessionTransport.get(), &WebViewTransport::sessionExpired, &login,
                         &LoginController::onSessionExpired);
    }

    // --- 5. QML -------------------------------------------------------------
    auto engine = std::make_unique<QQmlApplicationEngine>();
    QQmlContext *ctx = engine->rootContext();
    ctx->setContextProperty(QStringLiteral("bridge"), &bridge);
    ctx->setContextProperty(QStringLiteral("login"), &login);
    ctx->setContextProperty(QStringLiteral("chapel"), &chapel);
    ctx->setContextProperty(QStringLiteral("dining"), &dining);
    ctx->setContextProperty(QStringLiteral("semester"), &semester);
    // Read by every Theme.qml instance, which is how eight separate copies of
    // the palette agree on which one is showing.
    ctx->setContextProperty(QStringLiteral("settings"), &settings);
    ctx->setContextProperty(QStringLiteral("platformSurface"), surfaceQml);
    // Desktop only: the persistent profile WebSurfaceDesktop.qml binds to.
    ctx->setContextProperty(QStringLiteral("webProfile"), backend->qmlProfile());

    engine->loadFromModule("CedarView", "Main");
    if (engine->rootObjects().isEmpty()) {
        qCCritical(lcApp) << "QML failed to load. On desktop this is almost always QtWebEngine "
                             "missing from QML2_IMPORT_PATH — are you in the nix devShell?";
        return 1;
    }

    if (options.demo) {
        // Nothing will trigger the first load, since there is no login flow.
        chapel.refreshAll();
        dining.refreshAll();
    }

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
