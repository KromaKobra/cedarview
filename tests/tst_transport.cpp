// URL resolution, session-expiry detection, Response and FixtureTransport.
//
// Expiry detection is the part worth being paranoid about: a false negative
// sends a login page to a parser (confusing error, no recovery), and a false
// positive bounces you to a sign-in you did not need.

#include "testsupport.h"

#include "core/providers/dining.h"
#include "core/transport.h"
#include "ui/demotransport.h"
#include "ui/webviewtransport.h"

using namespace mycu;

class TestTransport : public QObject
{
    Q_OBJECT

private slots:
    // ---- URLs -------------------------------------------------------------

    void resolve_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("expected");
        QTest::newRow("rooted") << "/cedarinfo/chapelskip" << BASE_URL + "/cedarinfo/chapelskip";
        QTest::newRow("bare") << "cedarinfo/chapelskip" << BASE_URL + "/cedarinfo/chapelskip";
        QTest::newRow("absolute") << BASE_URL + "/Student/Grades" << BASE_URL + "/Student/Grades";
        QTest::newRow("elsewhere") << "https://example.com/x" << "https://example.com/x";
    }
    void resolve()
    {
        QFETCH(QString, path);
        QFETCH(QString, expected);
        QCOMPARE(mycu::resolve(path), expected);
    }

    void isSelfservice()
    {
        QVERIFY(mycu::isSelfservice(BASE_URL + "/cedarinfo/chapelskip"));
        QVERIFY(!mycu::isSelfservice("https://login.microsoftonline.com/x/saml2"));
    }

    void originOf()
    {
        QCOMPARE(mycu::originOf("/cedarinfo/chapelskip"), BASE_URL);
        QCOMPARE(mycu::originOf("https://diningdata.cedarville.edu/api/menus?days=2"),
                 DINING_BASE);
    }

    // ---- Expiry detection ---------------------------------------------------

    void idpUrlsAreDetected_data()
    {
        QTest::addColumn<QString>("url");
        QTest::newRow("saml") << "https://login.microsoftonline.com/81c32413-015d-4ba8-a93b-e1c28e355738/saml2?SAMLRequest=x";
        QTest::newRow("microsoft") << "https://login.microsoft.com/common/oauth2/authorize";
        QTest::newRow("windows") << "https://login.windows.net/common/";
        QTest::newRow("case must not matter") << "https://LOGIN.MICROSOFTONLINE.COM/x";
    }
    void idpUrlsAreDetected()
    {
        QFETCH(QString, url);
        QVERIFY(looksLikeLogin(url));
    }

    void selfserviceUrlsAreNotMistakenForLogin_data()
    {
        QTest::addColumn<QString>("url");
        QTest::newRow("chapel") << BASE_URL + "/cedarinfo/chapelskip";
        QTest::newRow("grades") << BASE_URL + "/Student/Grades";
        QTest::newRow("empty") << QString();
    }
    void selfserviceUrlsAreNotMistakenForLogin()
    {
        QFETCH(QString, url);
        QVERIFY(!looksLikeLogin(url));
    }

    // SAML posts the response back through the SP, so the URL can lie.
    void loginBodyIsDetectedEvenOnTheSelfserviceOrigin()
    {
        QVERIFY(looksLikeLogin(BASE_URL + "/cedarinfo/chapelskip", testing::loginHtml()));
    }

    // Empty responses happen for unrelated reasons; guessing would send the
    // user to a sign-in page they did not need.
    void aShortBodyIsNotTreatedAsExpiry()
    {
        QVERIFY(!looksLikeLogin(BASE_URL + "/cedarinfo/chapelskip", ""));
        QVERIFY(!looksLikeLogin(BASE_URL + "/cedarinfo/chapelskip", "{}"));
    }

    void realDataIsNotMistakenForALoginPage()
    {
        QVERIFY(!looksLikeLogin(BASE_URL + "/cedarinfo/chapelskip", testing::chapelHtml()));
    }

    // ---- Response -----------------------------------------------------------

    void raiseForSessionLetsGoodResponsesThrough()
    {
        const Response response{200, BASE_URL + "/cedarinfo/chapelskip", testing::chapelHtml(), {}};
        QCOMPARE(&response.raiseForSession(), &response);
    }

    void raiseForSessionCatchesARedirectToTheIdp()
    {
        const Response response{200, "https://login.microsoftonline.com/x/saml2", testing::loginHtml(), {}};
        QVERIFY_THROWS_EXCEPTION(SessionExpired, response.raiseForSession());
    }

    void jsonErrorIncludesTheBodySoYouCanSeeItWasHtml()
    {
        const Response response{200, BASE_URL + "/x", "<!DOCTYPE html><html>…", {}};
        CV_VERIFY_THROWS_MATCHING(response.json(), ParseError, "DOCTYPE");
    }

    void okReflectsTheStatusCode()
    {
        QVERIFY((Response{200, "u", "", {}}.ok()));
        QVERIFY((Response{204, "u", "", {}}.ok()));
        QVERIFY(!(Response{302, "u", "", {}}.ok()));
        QVERIFY(!(Response{500, "u", "", {}}.ok()));
    }

    // ---- FixtureTransport ---------------------------------------------------

    void fixtureSlugs_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("slug");
        QTest::newRow("rooted") << "/cedarinfo/chapelskip" << "cedarinfo_chapelskip";
        QTest::newRow("bare") << "cedarinfo/chapelskip" << "cedarinfo_chapelskip";
        QTest::newRow("query dropped") << BASE_URL + "/cedarinfo/chapelskip?term=FA26" << "cedarinfo_chapelskip";
        QTest::newRow("lowercased") << "/Student/Grades" << "student_grades";
        // Otherwise a /api/menus fixture would collide with a Self-Service one.
        QTest::newRow("other origin") << DINING_BASE + "/api/menus?days=2"
                                      << "diningdata_cedarville_edu_api_menus";
    }
    void fixtureSlugs()
    {
        QFETCH(QString, path);
        QFETCH(QString, slug);
        QCOMPARE(FixtureTransport::slug(path), slug);
    }

    void fixtureTransportServesTheChapelPage()
    {
        const Response response = FixtureTransport(testing::fixturesDir()).get("/cedarinfo/chapelskip");
        QVERIFY(response.ok());
        QVERIFY(response.body.contains("Chapel Attendance"));
        QCOMPARE(response.url, BASE_URL + "/cedarinfo/chapelskip");
    }

    // A missing fixture is a routine thing to hit while adding a provider; the
    // error should say what to create, not just "not found".
    void fixtureTransportNamesWhatItLookedFor()
    {
        FixtureTransport transport(testing::fixturesDir());
        CV_VERIFY_THROWS_MATCHING(transport.get("/Student/Grades"), FixtureNotFound, "student_grades");
    }

    // This is how the app switches to a JSON payload without a code change.
    void jsonFixtureWinsOverHtml()
    {
        QTemporaryDir dir;
        auto write = [&](const QString &name, const QByteArray &text) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(text);
        };
        write("cedarinfo_chapelskip.html", "<html>html</html>");
        write("cedarinfo_chapelskip.json", R"({"from": "json"})");

        const Response response = FixtureTransport(dir.path()).get("/cedarinfo/chapelskip");
        QCOMPARE(response.json().toObject().value("from").toString(), QStringLiteral("json"));
    }

    // ---- Demo mode's re-dated menus -------------------------------------------

    void demoMenusCoverTheWindowAskedFor()
    {
        RedatedMenusTransport transport(std::make_shared<FixtureTransport>(testing::fixturesDir()));
        const auto days = DiningProvider::parse(transport.get(DiningProvider(nullptr, 3, QDate(2026, 12, 25)).path()));

        QCOMPARE(days.size(), 3);
        QCOMPARE(days[0].on, QDate(2026, 12, 25));
        QCOMPARE(days[2].on, QDate(2026, 12, 27));
        for (const DayMenu &day : days)
            QCOMPARE(day.forVenue(HOME_COOKING).size(), 3);
    }

    void demoMenusDefaultToToday()
    {
        auto transport = std::make_shared<RedatedMenusTransport>(
            std::make_shared<FixtureTransport>(testing::fixturesDir()));
        const auto days = DiningProvider(transport).fetch();
        QCOMPARE(days.size(), DEFAULT_DAYS);
        QCOMPARE(days[0].on, QDate::currentDate());
        QVERIFY(!homeCookingFor(days).isEmpty());
    }

    // Paging must not change what a day was showing.
    void aDemoDayKeepsItsMenuInAnyWindow()
    {
        RedatedMenusTransport transport(std::make_shared<FixtureTransport>(testing::fixturesDir()));
        const QDate on(2026, 10, 1);
        const auto alone = DiningProvider::parse(transport.get(DiningProvider(nullptr, 1, on).path()));
        const auto later = DiningProvider::parse(transport.get(DiningProvider(nullptr, 7, on.addDays(-3)).path()));
        QCOMPARE(later[3].on, on);
        QCOMPARE(later[3].forVenue(HOME_COOKING)[0].items[0].name,
                 alone[0].forVenue(HOME_COOKING)[0].items[0].name);
    }

    // ---- Expiry detection must key on the host, not the URL string ----------

    void anIdpInAQueryParameterIsNotAnExpiredSession_data()
    {
        QTest::addColumn<QString>("url");
        // Observed for real in a `scripts/discover meals` capture: this raised
        // SessionExpired because the IdP name appears in a query parameter.
        QTest::newRow("tracking pixel")
            << "https://t.vibe.co/pixel/s?aid=X&url=https://selfservice.cedarville.edu/Cedarinfo/Meals"
               "&ref=https://login.microsoftonline.com/&ts=1789620230162";
        QTest::newRow("returnUrl") << BASE_URL + "/Cedarinfo/Meals?returnUrl=https%3A%2F%2Flogin.microsoftonline.com%2F";
        QTest::newRow("analytics") << "https://analytics.example.com/collect?dr=https://login.microsoftonline.com/";
    }
    void anIdpInAQueryParameterIsNotAnExpiredSession()
    {
        QFETCH(QString, url);
        QVERIFY(!looksLikeLogin(url));
    }

    // Suffix matching must be on a dot boundary, or lookalike hosts would read
    // as trusted IdPs.
    void aLookalikeHostDoesNotCount()
    {
        QVERIFY(!looksLikeLogin("https://login.microsoftonline.com.example.net/saml2"));
        QVERIFY(!looksLikeLogin("https://notlogin.microsoftonline.com/saml2"));
    }

    void aRealIdpSubdomainStillCounts()
    {
        QVERIFY(looksLikeLogin("https://eu.login.microsoftonline.com/x/saml2"));
    }

    // ---- Routing ------------------------------------------------------------

    void theRouterSendsEachOriginToItsTransport()
    {
        auto selfservice = std::make_shared<FixtureTransport>(testing::fixturesDir());
        auto dining = std::make_shared<FixtureTransport>(testing::fixturesDir());
        TransportRouter router(selfservice);
        router.route(DINING_BASE + "/", dining);

        QCOMPARE(router.transportFor("/cedarinfo/chapelskip"), selfservice.get());
        QCOMPARE(router.transportFor(DINING_BASE + "/api/menus?days=2"), dining.get());
        QCOMPARE(router.transportFor("https://example.com/x"), selfservice.get());
    }

    // ---- Telling "the session ended" apart from "the network is down" --------

    // A browser refusing to *make* a request is the signature of a sign-in.
    //
    // Chromium reports a CORS refusal as a bare `TypeError: Failed to fetch`,
    // with no status and no body. Observed on the device: the WebView had been
    // redirected to Microsoft, every fetch back to Self-Service was refused,
    // and the app reported "Couldn't reach Self-Service" — offering no way to
    // sign in, which was the actual remedy.
    void aRefusedRequestIsRecognised_data()
    {
        QTest::addColumn<QString>("error");
        QTest::newRow("chromium") << "TypeError: Failed to fetch";
        QTest::newRow("lowercase") << "TypeError: failed to fetch";
        QTest::newRow("gecko") << "NetworkError when attempting to fetch resource.";
        QTest::newRow("webkit") << "TypeError: Load failed";
        QTest::newRow("cors") << "blocked by CORS policy";
    }
    void aRefusedRequestIsRecognised()
    {
        QFETCH(QString, error);
        QVERIFY(looksLikeCorsFailure(error));
    }

    // The cost of a false positive is bouncing the user to a pointless login.
    void anOrdinaryFailureIsNotMistakenForASignIn_data()
    {
        QTest::addColumn<QString>("error");
        QTest::newRow("empty") << "";
        QTest::newRow("timeout") << "timed out after 30s";
        QTest::newRow("500") << "HTTP 500";
        QTest::newRow("destroyed") << "the WebView was destroyed";
    }
    void anOrdinaryFailureIsNotMistakenForASignIn()
    {
        QFETCH(QString, error);
        QVERIFY(!looksLikeCorsFailure(error));
    }
};

CEDARVIEW_TEST_MAIN(TestTransport, QCoreApplication)
#include "tst_transport.moc"
