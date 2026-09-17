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
// NOT YET RUN ON A DEVICE. See docs/next-steps.md.

import QtQuick
import QtWebView

Item {
    id: root

    property string currentUrl: view.url.toString()
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

        // VERIFIED against the Qt 6.11 Android build, by reading
        // PySide6/Qt/qml/QtWebView/plugins.qmltypes out of the
        // android_aarch64 wheel rather than by guessing:
        //
        //   Enum LoadStatus = LoadStartedStatus, LoadStoppedStatus,
        //                     LoadSucceededStatus, LoadFailedStatus
        //   Signal loadingChanged(QQuickWebViewLoadRequest loadRequest)
        //   QQuickWebViewLoadRequest: url, status, errorString  (all readonly)
        //
        // So the spelling below is right and this is no longer the likeliest
        // source of a QML runtime error. If it ever does break,
        // `adb logcat | grep -i qml` shows it immediately.
        onLoadingChanged: function (request) {
            if (request.status === WebView.LoadFailedStatus) {
                console.warn("load failed:", request.url, request.errorString)
            }
        }
    }
}
