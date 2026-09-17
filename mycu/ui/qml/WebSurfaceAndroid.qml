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

        // VERIFY on-device: QtWebView's loadingChanged carries a
        // WebViewLoadRequest with `status` and `errorString`. Confirm the enum
        // spelling against the Qt 6.11 Android build — this is the single most
        // likely place for a QML runtime error, and `adb logcat | grep -i qml`
        // will show it immediately if it is wrong.
        onLoadingChanged: function (request) {
            if (request.status === WebView.LoadFailedStatus) {
                console.warn("load failed:", request.url, request.errorString)
            }
        }
    }
}
