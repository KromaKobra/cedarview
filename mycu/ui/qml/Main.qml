// The one window. Shared verbatim between desktop and Android.
//
// The only platform-dependent thing here is which file the `surfaceLoader`
// loads — handed in from Python as `platformSurface`, and resolved by
// mycu.platform.<backend>.surface_qml. Both surfaces expose the same members,
// so nothing else in the QML tree knows or cares.
//
// Layout, outermost first:
//
//   ribbon header      logo, wordmark, refresh, overflow — always visible
//   StackLayout
//     0 — the app: three tabs behind a bottom bar
//     1 — the web surface, shown ONLY during an interactive sign-in
//
// During normal operation the browser is invisible: it is an implementation
// detail of the transport, not a thing the user should have to look at.
//
// Colours come from Theme.qml and nothing here hard-codes one. There is no
// light mode; see Theme.qml for why.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 400
    height: 800
    title: "CedarView"
    color: theme.background

    // Phone-sized by default so the desktop build previews the Android layout.
    // On Android the window is fullscreen regardless.

    Theme { id: theme }

    property bool showingLogin: login.surfaceVisible

    //: Which tab is on screen. Kept here rather than in each view so the
    //: bottom bar and the SwipeView stay in step with one another.
    property int currentPage: 0

    property bool busy: chapel.busy || dining.busy

    // Every tab, not just the one on screen. The summary screen draws on both
    // viewmodels and on four separate services, so "refresh what I am looking
    // at" and "refresh everything" are the same gesture now — and a refresh
    // that reloaded only half of the screen in front of you would be the more
    // surprising of the two behaviours.
    function refreshEverything() {
        chapel.refreshAll()
        dining.refreshAll()
    }

    // ---- Ribbon ------------------------------------------------------------
    header: Rectangle {
        implicitHeight: 54
        color: theme.ribbon

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 6
            spacing: 9

            Glyph {
                Layout.alignment: Qt.AlignVCenter
                width: 24
                height: 24
                kind: "tree"
                color: theme.accent
            }

            Label {
                text: "CedarView"
                color: theme.text
                font.pixelSize: 19
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            BusyIndicator {
                Layout.alignment: Qt.AlignVCenter
                running: window.busy
                visible: running
                implicitWidth: 22
                implicitHeight: 22
            }

            AbstractButton {
                id: refreshButton
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 40
                implicitHeight: 40
                enabled: !window.showingLogin && !window.busy
                onClicked: window.refreshEverything()

                background: Rectangle {
                    radius: width / 2
                    color: refreshButton.down ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                }

                contentItem: Glyph {
                    kind: "refresh"
                    color: refreshButton.enabled ? theme.muted : theme.faint
                    width: 19
                    height: 19
                }
            }

            AbstractButton {
                id: overflowButton
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 40
                implicitHeight: 40
                onClicked: overflow.open()

                background: Rectangle {
                    radius: width / 2
                    color: overflowButton.down ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                }

                // Three drawn dots rather than "⋮". U+22EE happens to be in
                // Roboto, so the character would survive — but the refresh
                // glyph beside it would not, and a toolbar where one icon is a
                // character and the other is a drawing is a toolbar where the
                // two never quite line up.
                contentItem: Column {
                    spacing: 3

                    Repeater {
                        model: 3

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 3.5
                            height: 3.5
                            radius: 1.75
                            color: theme.muted
                        }
                    }
                }

                Menu {
                    id: overflow
                    y: overflowButton.height + 4
                    x: overflowButton.width - width
                    implicitWidth: 184
                    padding: 6

                    background: Rectangle {
                        color: "#1A1A1F"
                        radius: 14
                        border.width: 1
                        border.color: theme.cardBorder
                    }

                    DarkMenuItem {
                        text: "Settings"
                        onTriggered: settingsSheet.open()
                    }

                    DarkMenuItem {
                        text: "About"
                        onTriggered: aboutSheet.open()
                    }

                    DarkMenuItem {
                        text: "Sign out"
                        onTriggered: login.signOut()
                    }
                }
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: theme.divider
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: window.showingLogin ? 1 : 0

        // Page 0 of the login/data stack: the tabs, above the bottom bar.
        ColumnLayout {
            spacing: 0

            SwipeView {
                id: pages
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.currentPage
                onCurrentIndexChanged: window.currentPage = currentIndex

                SummaryView {}
                ChapelView {}
                DiningView {}
            }

            // ---- Bottom bar --------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 60
                color: theme.ribbon

                Rectangle {
                    anchors.top: parent.top
                    width: parent.width
                    height: 1
                    color: theme.divider
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.topMargin: 1
                    spacing: 0

                    NavButton {
                        Layout.fillWidth: true
                        text: "Summary"
                        kind: "summary"
                        selected: window.currentPage === 0
                        onClicked: window.currentPage = 0
                    }

                    NavButton {
                        Layout.fillWidth: true
                        text: "Chapel"
                        kind: "chapel"
                        selected: window.currentPage === 1
                        onClicked: window.currentPage = 1
                    }

                    NavButton {
                        Layout.fillWidth: true
                        text: "Dining"
                        kind: "dining"
                        selected: window.currentPage === 2
                        onClicked: window.currentPage = 2
                    }
                }
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
                color: theme.muted
                horizontalAlignment: Text.AlignHCenter
                text: "The embedded browser failed to load (" + platformSurface + ").\n\n"
                      + "On desktop this usually means QtWebEngine is not on QML2_IMPORT_PATH "
                      + "— check flake.nix. On Android it means the QtWebView module was not "
                      + "bundled — see docs/android.md."
            }
        }
    }

    footer: Rectangle {
        visible: login.status.length > 0
        implicitHeight: visible ? statusLabel.implicitHeight + 18 : 0
        color: theme.ribbon

        Label {
            id: statusLabel
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            text: login.status
            color: theme.muted
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
    }

    InfoSheet {
        id: aboutSheet
        heading: "CedarView"
        body: "A personal client for your own Cedarville records.\n\n"
              + "Backend: " + bridge.platformName + "\n\n"
              + "Your password is never seen or stored by this app — sign-in "
              + "happens on Microsoft's own page."
    }

    InfoSheet {
        id: settingsSheet
        heading: "Settings"
        body: "Coming soon!\n\nThere is nothing to configure yet: the app reads "
              + "your own records on demand and keeps its session in app-private "
              + "storage. Sign out from the same menu to clear it."
    }
}
