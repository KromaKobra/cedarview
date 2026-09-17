// Offline stand-in for the web surface, used by `--demo`.
//
// Interface-identical to the two real surfaces, but backed by nothing. It lets
// the whole UI be developed, screenshotted and reviewed against fixtures with
// no login, no network, and without loading QtWebEngine at all — which matters
// because QtWebEngine takes a second to start and needs a working GPU stack.

import QtQuick

Item {
    id: root

    property string currentUrl: "https://selfservice.cedarville.edu/cedarinfo/chapelskip"
    signal evalResult(string token, var result)

    function evalAsync(token, script) {
        // Nothing ever answers. The demo transport serves fixtures directly and
        // never reaches the surface, so this is unreachable in practice — it
        // exists so the QML tree is identical in both modes.
        console.log("demo surface: ignoring script for token", token)
    }

    function navigate(url) {
        console.log("demo surface: ignoring navigation to", url)
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.05)

        Text {
            anchors.centerIn: parent
            text: "demo mode — no browser"
            opacity: 0.5
        }
    }
}
