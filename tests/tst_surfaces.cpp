// The contract between the web surfaces, and the build that picks one.
//
// None of this needs a phone. Backend selection itself is compile-time now
// (CMakeLists.txt builds desktop.cpp or android.cpp), so what is left to check
// is the one invariant that would otherwise only break on-device: every
// surface exposes the same interface, and each imports only its own web
// module.

#include "testsupport.h"

#include <QRegularExpression>

namespace {

// Everything WebViewTransport and Main.qml use on a surface.
const QStringList SURFACE_CONTRACT = {"currentUrl", "evalResult", "evalAsync", "navigate"};

const QStringList SURFACES = {"WebSurfaceDesktop.qml", "WebSurfaceAndroid.qml", "WebSurfaceStub.qml"};

QString qml(const QString &name)
{
    return testing::readText(testing::sourceDir() + "/qml/" + name);
}

QString stripComments(const QString &source)
{
    static const QRegularExpression comment(QStringLiteral("//[^\n]*"));
    QString out = source;
    out.remove(comment);
    return out;
}

// The modules a QML file imports. Comments mentioning them do not count.
QStringList imports(const QString &name)
{
    static const QRegularExpression importLine(QStringLiteral("^\\s*import\\s+([\\w.]+)"),
                                               QRegularExpression::MultilineOption);
    QStringList out;
    auto it = importLine.globalMatch(qml(name));
    while (it.hasNext())
        out.append(it.next().captured(1));
    return out;
}

} // namespace

class TestSurfaces : public QObject
{
    Q_OBJECT

private slots:
    void everySurfaceHonoursTheSameContract_data()
    {
        QTest::addColumn<QString>("name");
        for (const QString &name : SURFACES)
            QTest::newRow(qPrintable(name)) << name;
    }
    void everySurfaceHonoursTheSameContract()
    {
        QFETCH(QString, name);
        const QString source = qml(name);
        for (const QString &member : SURFACE_CONTRACT)
            QVERIFY2(source.contains(member), qPrintable(name + " is missing " + member));
    }

    // The transport tags each request; results must come back tagged. Without
    // the token the poller cannot tell two in-flight requests apart. And the
    // C++ side connects to exactly `evalResult(QString,QVariant)`, which is
    // what `string` and `var` become.
    void surfacesDeliverResultsThroughTheTaggedSignal_data() { everySurfaceHonoursTheSameContract_data(); }
    void surfacesDeliverResultsThroughTheTaggedSignal()
    {
        QFETCH(QString, name);
        static const QRegularExpression signal(
            QStringLiteral(R"(signal\s+evalResult\s*\(\s*string\s+token\s*,\s*var\s+result\s*\))"));
        QVERIFY2(signal.match(qml(name)).hasMatch(), qPrintable(name));
    }

    // WebViewTransport follows `currentUrl` through its change signal, so the
    // property must be a plain property (which gets one) on every surface.
    void currentUrlIsAPropertyEverywhere_data() { everySurfaceHonoursTheSameContract_data(); }
    void currentUrlIsAPropertyEverywhere()
    {
        QFETCH(QString, name);
        static const QRegularExpression property(QStringLiteral(R"(property\s+string\s+currentUrl\b)"));
        QVERIFY2(property.match(qml(name)).hasMatch(), qPrintable(name));
    }

    // Regression, found on the device: sign-in was impossible on Android.
    //
    // QtWebView's `url` property is the URL that was *requested*, not the one
    // the browser ended up on, so a server-side redirect — which is exactly how
    // the SAML sign-in begins — never updates it. `currentUrl` was bound to it,
    // the login state machine never saw login.microsoftonline.com, the sign-in
    // surface never opened, and the app showed a CORS error with no way to
    // authenticate.
    //
    // The load request's url is the only place the post-redirect URL appears,
    // so assert the handler reads it. Asserted against the source because the
    // bug is in QML wiring, which no headless test can execute.
    void theAndroidSurfaceTakesItsUrlFromTheLoadRequest()
    {
        const QString source = stripComments(qml("WebSurfaceAndroid.qml"));

        static const QRegularExpression handler(
            QStringLiteral(R"(onLoadingChanged\s*:\s*function\s*\((\w+)\)\s*\{(.*?)\n        \})"),
            QRegularExpression::DotMatchesEverythingOption);
        const auto match = handler.match(source);
        QVERIFY2(match.hasMatch(), "WebSurfaceAndroid.qml has no onLoadingChanged handler");

        const QString param = match.captured(1);
        const QString body = match.captured(2);
        QVERIFY2(body.contains(param + ".url"),
                 "onLoadingChanged must set currentUrl from the load request's url; without it a redirect "
                 "to the identity provider is invisible and interactive sign-in cannot start.");
        QVERIFY2(body.contains("currentUrl"), "the load request's url must reach currentUrl");

        static const QRegularExpression boundToViewUrl(
            QStringLiteral(R"(property\s+string\s+currentUrl\s*:\s*view\.url)"));
        QVERIFY2(!boundToViewUrl.match(source).hasMatch(),
                 "currentUrl must not be bound to view.url — on QtWebView that property does not follow "
                 "redirects. Assign it from onLoadingChanged instead.");
    }

    // --demo must not need QtWebEngine — that is the whole point of it.
    void theStubSurfaceImportsNothingPlatformSpecific()
    {
        QCOMPARE(imports("WebSurfaceStub.qml"), QStringList{"QtQuick"});
    }

    // Importing the wrong one fails at QML load time, on the device.
    //
    // QtWebEngine does not exist on Android and QtWebView is not linked on the
    // desktop, so a stray import is a startup failure on whichever platform is
    // not the one you tested.
    void eachRealSurfaceImportsOnlyItsOwnBackend()
    {
        const QStringList desktop = imports("WebSurfaceDesktop.qml");
        const QStringList android = imports("WebSurfaceAndroid.qml");
        QVERIFY(desktop.contains("QtWebEngine") && !desktop.contains("QtWebView"));
        QVERIFY(android.contains("QtWebView") && !android.contains("QtWebEngine"));
    }

    // Everything but the surfaces ships to both platforms.
    void theSharedQmlNeverImportsAWebModule()
    {
        QDir dir(testing::sourceDir() + "/qml");
        for (const QString &name : dir.entryList({"*.qml"}, QDir::Files)) {
            if (name.startsWith("WebSurface"))
                continue;
            for (const QString &module : imports(name))
                QVERIFY2(!module.startsWith("QtWeb"), qPrintable(name + ": " + module));
        }
    }

    // Each platform's build gets exactly its own surface, plus the stub; the
    // import scanner that fills the APK would otherwise go looking for
    // QtWebEngine on Android.
    void theBuildGivesEachPlatformOnlyItsOwnSurface()
    {
        const QString cmake = testing::readText(testing::sourceDir() + "/CMakeLists.txt");
        static const QRegularExpression perPlatform(QStringLiteral(
            R"(if\(ANDROID\)\s*list\(APPEND CEDARVIEW_QML qml/WebSurfaceAndroid\.qml\)\s*else\(\)\s*)"
            R"(list\(APPEND CEDARVIEW_QML qml/WebSurfaceDesktop\.qml\)\s*endif\(\))"));
        QVERIFY(perPlatform.match(cmake).hasMatch());
        // …and neither real surface is in the shared list.
        const qsizetype shared = cmake.indexOf("set(CEDARVIEW_QML");
        const QString sharedList = cmake.mid(shared, cmake.indexOf(u')', shared) - shared);
        QVERIFY(!sharedList.contains("WebSurfaceAndroid"));
        QVERIFY(!sharedList.contains("WebSurfaceDesktop"));
        QVERIFY(sharedList.contains("WebSurfaceStub"));
    }
};

CEDARVIEW_TEST_MAIN(TestSurfaces, QCoreApplication)
#include "tst_surfaces.moc"
