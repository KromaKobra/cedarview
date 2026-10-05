// Desktop web surface — QtWebEngine.
//
// This file and WebSurfaceAndroid.qml expose an IDENTICAL interface. If you
// change one, change the other. Everything else in the app talks only to these
// seven members:
//
//   property string currentUrl            the page we are on right now
//   property bool loading                 a page is loading (SignInView's bar)
//   property int loadProgress             0–100, how far
//   signal  pageLoaded(url)               a page finished loading, and where
//   signal  evalResult(token, result)     a runJavaScript result, tagged
//   function evalAsync(token, script)     run JS, deliver via evalResult
//   function navigate(url)                go somewhere
//
// `currentUrl` changes the moment navigate() is called, to the address asked
// for, before any redirect; `pageLoaded` comes only once a page is really
// there. The sign-in flow needs both (see LoginController::onPageLoaded).
//
// Nothing here reads cookies. See src/ui/webviewtransport.h for why: the
// Android backend cannot, so neither does this one, and there is exactly one
// code path to debug.

import QtQuick
import QtWebEngine

Item {
    id: root

    property string currentUrl: view.url.toString()
    readonly property bool loading: view.loading
    readonly property int loadProgress: view.loadProgress
    signal pageLoaded(string url)
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

        // The persistent profile from DesktopBackend::configureProfile. Without
        // it the view uses Qt's default profile, which is off-the-record, and
        // the sign-in (and its MFA prompt) is lost on every relaunch.
        profile: webProfile

        // A blank start page: main.cpp decides where to go, via
        // LoginController::begin(), so that RelayState carries the return path.
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
            if (info.status === WebEngineView.LoadSucceededStatus) {
                root.pageLoaded(info.url.toString())
            } else if (info.status === WebEngineView.LoadFailedStatus) {
                console.warn("load failed:", info.url, info.errorString)
            }
        }
    }
}
