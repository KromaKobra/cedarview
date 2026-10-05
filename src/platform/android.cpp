// Android backend: QtWebView (the system WebView).
//
// This is the only file that Android needs and desktop never compiles.
//
// Why the transport works the way it does
// ---------------------------------------
//
// QtWebEngine does not exist on Android. Its substitute, QtWebView, wraps the
// OS WebView, and Qt's documentation is explicit about the consequence:
//
//     "When Qt WebEngine module is used as backend, cookieAdded signal will be
//     emitted for any cookie added to the underlying QWebEngineCookieStore,
//     including those added by websites. **In other cases cookieAdded signal
//     is only emitted for cookies explicitly added with setCookie().**"
//
// So on Android we cannot read the HttpOnly ASP.NET session cookie, and
// therefore cannot hand it to an HTTP client. We do not need to. The WebView
// already has the cookie and attaches it automatically — so we let *it* make
// the request, from inside the logged-in page, and pass the body back through
// runJavaScript. See src/ui/webviewtransport.h.
//
// Since v0.2.1 that is the desktop's story only. On Android the cookie is
// read from android.webkit.CookieManager instead, and Self-Service goes over
// plain HTTPS, because QtWebView 6.11's runJavaScript runs its callback on the
// wrong thread and crashed the app. See android_sessiontransport.h.

#include "backend.h"

#include "core/log.h"

#include <QJniEnvironment>
#include <QJniObject>
#include <QtWebView/qtwebviewfunctions.h>

#include <stdexcept>

namespace mycu {

namespace {

class AndroidBackend final : public WebBackend
{
public:
    QString name() const override { return QStringLiteral("android"); }
    QString surfaceQml() const override { return QStringLiteral("WebSurfaceAndroid.qml"); }

    // Qt 6 documents QtWebView::initialize() as belonging before the
    // QGuiApplication is constructed (Qt 5 had it after). For the native
    // Android plugin it has nothing to prepare, but calling it where the docs
    // say keeps a future plugin — or a future Qt — from failing silently.
    // Without QtWebView there is no way to log in at all.
    void beforeApp() override
    {
        QtWebView::initialize();
        qCInfo(lcPlatform) << "QtWebView initialised";
    }

    // No-op: the system WebView owns its cookie jar and already keeps it in
    // app-private storage, which is where we wanted it.
    void configureProfile(const QString &) override
    {
        qCDebug(lcPlatform) << "android: cookie persistence is handled by the system WebView";
    }

    // QtWebView has no cookie API, but the WebView's own CookieManager does.
    // This is the whole of sign-out on the phone: Self-Service's session
    // cookie and Microsoft's go together, so the app neither relaunches signed
    // in nor signs straight back in without asking.
    void clearCookies() override
    {
        QJniObject::callStaticMethod<void>("com/kromakobra/cedarview/WebViewCookies", "clear");
        QJniEnvironment env;
        if (env.checkAndClearExceptions())
            throw std::runtime_error("CookieManager could not clear the WebView's cookies");
        qCInfo(lcPlatform) << "android cookies cleared";
    }
};

} // namespace

WebBackend *createBackend()
{
    return new AndroidBackend;
}

} // namespace mycu
