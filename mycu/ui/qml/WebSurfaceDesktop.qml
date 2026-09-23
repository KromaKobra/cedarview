// Desktop web surface — QtWebEngine.
//
// This file and WebSurfaceAndroid.qml expose an IDENTICAL interface. If you
// change one, change the other. Everything else in the app talks only to these
// four members:
//
//   property string currentUrl            the page we are on right now
//   signal  evalResult(token, result)     a runJavaScript result, tagged
//   function evalAsync(token, script)     run JS, deliver via evalResult
//   function navigate(url)                go somewhere
//
// Nothing here reads cookies. See mycu/ui/transport_webview.py for why: the
// Android backend cannot, so neither does this one, and there is exactly one
// code path to debug.

import QtQuick
import QtWebEngine

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

    WebEngineView {
        id: view
        anchors.fill: parent

        // The persistent profile from DesktopBackend.configure_profile. Without
        // it the view uses Qt's default profile, which is off-the-record, and
        // the sign-in (and its MFA prompt) is lost on every relaunch.
        profile: webProfile

        // A blank start page: app.py decides where to go, via
        // LoginController.begin(), so that RelayState carries the return path.
        url: "about:blank"

        onUrlChanged: root.currentUrl = view.url.toString()

        // Entra ID sometimes opens a popup (device-code prompts, "stay signed
        // in?" variants, some MFA providers). Without this the window is
        // created and immediately discarded, and the sign-in silently stalls.
        onNewWindowRequested: function (request) {
            request.openIn(view)
        }

        // Surface certificate problems rather than swallowing them. We never
        // want to accept a bad cert for an identity provider.
        onCertificateError: function (error) {
            console.warn("certificate error for", error.url, "-", error.description)
            error.rejectCertificate()
        }

        onLoadingChanged: function (info) {
            if (info.status === WebEngineView.LoadFailedStatus) {
                console.warn("load failed:", info.url, info.errorString)
            }
        }
    }
}
