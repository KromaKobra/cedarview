// Android only: Self-Service requests over HTTPS, carrying the WebView's
// session cookies. No JavaScript is involved.
//
// Why not WebViewTransport, as on the desktop
// -------------------------------------------
//
// WebViewTransport runs fetch() inside the page through runJavaScript. In Qt
// 6.11, QtWebView on Android answers every runJavaScript call by invoking the
// callback directly from WebView.evaluateJavascript's ValueCallback, on the
// Android UI thread:
//
//   ValueCallback.onReceiveValue  (Android UI thread)
//     -> c_onRunJavaScriptResult -> javaScriptResult()     qandroidwebview.cpp
//     -> the lambda in QQuickWebView::runJavaScript        qquickwebview.cpp
//     -> QJSEngine::toScriptValue + QJSValue::call
//
// Qt's GUI thread is a different thread on Android, and nothing along that
// path moves the call across. So the QML callback ran alongside the GUI
// thread's own use of the QML engine, and the heap was corrupted at random.
// v0.2.0 closed on most refreshes, and the crash logs show SIGSEGV inside
// libQt6Qml on the Android main thread. (A bridge that called
// evaluateJavascript itself was tried first. It cannot work: Qt detaches the
// WebView from the view tree whenever the sign-in surface is hidden, and
// keeps no other reference to it that we can reach.)
//
// What makes this possible
// ------------------------
//
// The original reason for the in-page fetch was that Qt cannot read the
// HttpOnly session cookie on Android. android.webkit.CookieManager can: it is
// the WebView's own cookie jar, shared by the whole process, HttpOnly cookies
// included. So the request goes through HttpGet.getWithWebViewCookies() with
// those cookies, and cookies the server sets are written back, so the WebView
// and this transport share one session.
//
// The WebView is still what signs you in. This transport only reads with the
// session it left behind. When that session has ended, Self-Service redirects
// to Microsoft. The Java side stops at that redirect without following it, and
// this throws SessionExpired, which reopens the sign-in surface as before.

#pragma once

#include "core/transport.h"

namespace mycu {

class AndroidSessionTransport : public Transport
{
public:
    explicit AndroidSessionTransport(int timeoutMs = 30000);

    // Blocks, like every transport; the providers call it on pool threads.
    // Throws SessionExpired when the request lands on an identity provider,
    // TransportError for anything else that is not a 2xx.
    Response get(const QString &path) override;

private:
    int m_timeoutMs;
};

} // namespace mycu
