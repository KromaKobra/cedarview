// Android web surface — QtWebView (wraps the system WebView).
//
// Interface-identical to WebSurfaceDesktop.qml. If you change one, change the
// other; the rest of the app cannot tell them apart.
//
// QtWebView is a much thinner type than WebEngineView: no popup hook, no
// certificate-error signal, no cookie API, no profile. It has `url`,
// `loading`, `loadProgress` and `runJavaScript` — which is precisely the set
// the transport needs, and the reason the Android port is wiring rather than
// research.
//
// RUN ON A DEVICE (moto g power 5G, Android 15). See docs/android-status.md.

import QtQuick
import QtWebView

Item {
    id: root

    // Plain property, deliberately not bound to `view.url`.
    //
    // It used to be `property string currentUrl: view.url.toString()`, which
    // reads as the obvious thing and is wrong twice over: the handlers below
    // assign to it, which silently destroys the binding anyway, and — the part
    // that actually broke sign-in — `view.url` is NOT the address bar. On
    // QtWebView it reports the URL that was *requested*, and a server-side
    // redirect never updates it. See onLoadingChanged.
    property string currentUrl: ""
    signal evalResult(string token, var result)

    function evalAsync(token, script) {
        view.runJavaScript(script, function (result) {
            root.evalResult(token, result)
        })
    }

    function navigate(url) {
        view.url = url
    }

    WebView {
        id: view
        anchors.fill: parent
        url: "about:blank"

        onUrlChanged: root.currentUrl = view.url.toString()

        // THE load signal, not a diagnostic one: this is where the real URL
        // arrives, and the whole login state machine is driven by "which URL
        // did we end up on".
        //
        // Observed on the device, 2026-09-17: requesting /cedarinfo/chapelskip
        // redirected to login.microsoftonline.com — the Chromium console
        // proved it, by refusing a fetch from that origin — while the view's
        // own url property still read the Self-Service address we had asked
        // for. So urlChanged never fired for Microsoft's page,
        // looks_like_login never saw it, the sign-in surface never opened, and
        // the app sat on a CORS error with no way to authenticate. Taking the
        // URL from the load request is what makes interactive sign-in possible
        // on Android at all.
        //
        // The members used here are VERIFIED against the Qt 6.11 Android
        // build, by reading PySide6/Qt/qml/QtWebView/plugins.qmltypes out of
        // the android_aarch64 wheel rather than by guessing:
        //
        //   Enum LoadStatus = LoadStartedStatus, LoadStoppedStatus,
        //                     LoadSucceededStatus, LoadFailedStatus
        //   Signal loadingChanged(QQuickWebViewLoadRequest loadRequest)
        //   QQuickWebViewLoadRequest: url, status, errorString  (all readonly)
        onLoadingChanged: function (request) {
            if (request.url) {
                root.currentUrl = request.url.toString()
            }
            if (request.status === WebView.LoadFailedStatus) {
                console.warn("load failed:", request.url, request.errorString)
            }
        }
    }
}
