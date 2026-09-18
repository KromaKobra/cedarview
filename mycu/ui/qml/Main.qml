// The one window. Shared verbatim between desktop and Android.
//
// The only platform-dependent thing here is which file the `surfaceLoader`
// loads — handed in from Python as `platformSurface`, and resolved by
// mycu.platform.<backend>.surface_qml. Both surfaces expose the same members,
// so nothing else in the QML tree knows or cares.
//
// Layout: a StackLayout with two pages.
//   0 — the data UI (chapel, plus whatever providers come later)
//   1 — the web surface, shown ONLY while an interactive sign-in is happening
//
// During normal operation the browser is invisible: it is an implementation
// detail of the transport, not a thing the user should have to look at.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 420
    height: 760
    title: "myCU"

    // Phone-sized by default so the desktop build previews the Android layout.
    // On Android the window is fullscreen regardless.

    property bool showingLogin: login.surfaceVisible

    // Which data page is on screen. Kept here rather than in each view so the
    // toolbar's title and ↻ button know what they are acting on.
    property int currentPage: 0
    readonly property var pageTitles: ["Chapel", "Dining"]
    readonly property string pageTitle: pageTitles[currentPage]

    // refreshAll(), not refresh(): each screen shows more than one source, and
    // the viewmodel is what knows which ones belong to it.
    function refreshCurrent() {
        if (currentPage === 0) {
            chapel.refreshAll()
        } else {
            dining.refreshAll()
        }
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12

            Label {
                text: window.showingLogin ? "Sign in" : window.pageTitle
                font.pixelSize: 20
                font.bold: true
                Layout.fillWidth: true
            }

            BusyIndicator {
                running: chapel.busy || dining.busy
                visible: running
                implicitWidth: 24
                implicitHeight: 24
            }

            ToolButton {
                // Plain word, not a glyph. This was "↻" (U+21BB), which the
                // desktop font has and the phone's Roboto does not — on the
                // moto g power it rendered as a tofu box. "⋮" below survives
                // because U+22EE *is* in Roboto, so the two are not
                // interchangeable risks. Anything outside basic Latin needs
                // checking on-device before it goes in the toolbar.
                text: qsTr("Refresh")
                enabled: !window.showingLogin
                onClicked: window.refreshCurrent()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Refresh")
            }

            ToolButton {
                text: "⋮"                       // ⋮
                font.pixelSize: 20
                onClicked: overflow.open()

                Menu {
                    id: overflow
                    y: parent.height
                    MenuItem {
                        text: "Sign out"
                        onTriggered: login.signOut()
                    }
                    MenuItem {
                        text: "About"
                        onTriggered: aboutDialog.open()
                    }
                }
            }
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: window.showingLogin ? 1 : 0

        // Page 0 of the login/data stack: the data pages, behind a tab bar.
        ColumnLayout {
            spacing: 0

            SwipeView {
                id: pages
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.currentPage
                onCurrentIndexChanged: window.currentPage = currentIndex

                ChapelView {}
                DiningView {}
            }

            TabBar {
                Layout.fillWidth: true
                currentIndex: window.currentPage
                onCurrentIndexChanged: window.currentPage = currentIndex

                TabButton { text: "Chapel" }
                TabButton { text: "Dining" }
            }
        }

        // The web surface. Kept loaded at all times — it holds the session, and
        // unloading it would throw away the cookie jar on Android.
        Item {
            Loader {
                id: surfaceLoader
                anchors.fill: parent
                source: platformSurface          // context property from app.py
                asynchronous: false

                onLoaded: {
                    // Hand the live surface object to the Python transport and
                    // wire the URL feed into the login state machine.
                    bridge.attachSurface(item)
                    item.currentUrlChanged.connect(function () {
                        login.onUrlChanged(item.currentUrl)
                    })
                    login.begin(bridge.startPath)
                }

                Connections {
                    target: login
                    function onNavigateRequested(url) {
                        if (surfaceLoader.item) {
                            surfaceLoader.item.navigate(url)
                        }
                    }
                }
            }

            // Failing to load the surface means no login is possible at all, so
            // say so plainly instead of showing an empty rectangle.
            Label {
                anchors.centerIn: parent
                visible: surfaceLoader.status === Loader.Error
                wrapMode: Text.Wrap
                width: parent.width - 48
                horizontalAlignment: Text.AlignHCenter
                text: "The embedded browser failed to load (" + platformSurface + ").\n\n"
                      + "On desktop this usually means QtWebEngine is not on QML2_IMPORT_PATH "
                      + "— check flake.nix. On Android it means the QtWebView module was not "
                      + "bundled — see docs/android.md."
            }
        }
    }

    footer: Label {
        visible: text.length > 0
        text: login.status
        padding: 10
        wrapMode: Text.Wrap
        font.pixelSize: 13
        opacity: 0.75
    }

    Dialog {
        id: aboutDialog
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 360)
        title: "myCU"
        standardButtons: Dialog.Ok

        Label {
            // availableWidth, not parent.width. The Dialog derives its
            // implicitHeight from this Label, so binding the Label's width to
            // the Dialog's own width closes the loop and Qt logs
            // "Binding loop detected for property implicitHeight" on every
            // launch. availableWidth is the content box and does not depend
            // on the content.
            width: aboutDialog.availableWidth
            wrapMode: Text.Wrap
            text: "A personal client for your own Cedarville records.\n\n"
                  + "Backend: " + bridge.platformName + "\n"
                  + "Your password is never seen or stored by this app — sign-in "
                  + "happens on Microsoft's own page."
        }
    }
}
