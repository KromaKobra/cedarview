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
//     0 — the app: four tabs behind a bottom bar
//     1 — the web surface, shown ONLY during an interactive sign-in
//
// During normal operation the browser is invisible: it is an implementation
// detail of the transport, not a thing the user should have to look at.
//
// Colours come from Theme.qml and nothing here hard-codes one. Dark and light
// are both live; the switch is in the overflow menu under Settings.

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

    // ---- Edge to edge ------------------------------------------------------
    //
    // From targetSdk 35 Android draws every app edge to edge, and at 36 the
    // opt-out is gone: the window runs under the status bar at the top and the
    // gesture handle at the bottom. Qt reports those strips as safe-area
    // margins. Since 6.9, ApplicationWindow pads its contentItem by them on
    // its own, but NOT the header or the footer (see the ApplicationWindow
    // docs), so those two take their insets explicitly below.
    //
    // The bottom padding is switched off and handed to the bars themselves, so
    // the ribbon colour runs on under the gesture handle rather than stopping
    // above it with a strip of background showing. SafeArea margins are
    // relative to each item's own geometry, so whichever bar is actually at
    // the bottom edge — the tab bar, or the status footer while it is showing
    // — picks up the inset, and the other gets zero.
    //
    // On desktop every margin is zero, and all of this is a no-op.
    bottomPadding: 0

    //: Where the privacy policy lives. Google Play requires the link both in the
    //: store listing and inside the app, so the overflow menu has one.
    readonly property string privacyPolicyUrl:
        "https://github.com/KromaKobra/cedarview/blob/main/PRIVACY.md"

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
        semester.refreshAll()
    }

    // ---- Ribbon ------------------------------------------------------------
    header: Rectangle {
        id: ribbon
        // The status bar's height on top of the ribbon's own, so the colour
        // fills in behind the clock and the controls start below it.
        implicitHeight: 54 + ribbon.SafeArea.margins.top
        color: theme.ribbon

        RowLayout {
            anchors.fill: parent
            anchors.topMargin: ribbon.SafeArea.margins.top
            anchors.leftMargin: 14 + ribbon.SafeArea.margins.left
            anchors.rightMargin: 6 + ribbon.SafeArea.margins.right
            spacing: 9

            // The one raster image in the app. It lives beside the QML rather
            // than in assets/ for a build reason worth knowing: scripts/build-apk
            // stages only main.py, pyproject.toml and mycu/ into android-build/,
            // so anything under assets/ is on the desktop and nowhere else. A
            // relative source resolves against this file's own directory, which
            // is the same path on both platforms.
            Image {
                Layout.alignment: Qt.AlignVCenter
                // Layout.preferred*, not width/height: a RowLayout sizes its
                // children from their implicit size, and `sourceSize` below
                // *is* an Image's implicit size — so a plain `width: 26` here
                // is overruled and the logo comes out 52px tall.
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                source: "icon.png"
                // Decoded at 2x and mipmapped: the source is 512px square, and
                // scaling that down without either of these is a smear.
                sourceSize: Qt.size(52, 52)
                fillMode: Image.PreserveAspectFit
                mipmap: true
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
                    color: refreshButton.down ? theme.pressed : "transparent"
                }

                // Wrapped in an Item so the glyph can be anchored. Control sets
                // its contentItem's *position* to (leftPadding, topPadding) and
                // its size to the available space; giving an item an explicit
                // width and height overrides the size but not the position, so
                // a bare `Glyph { width: 10 }` here would sit in the top-left
                // corner of the 40px button. That is the bug this shape fixes,
                // and the overflow button below has the same one.
                contentItem: Item {
                    Glyph {
                        anchors.centerIn: parent
                        kind: "refresh"
                        color: refreshButton.enabled ? theme.muted : theme.faint
                        width: 15
                        height: 15
                    }
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
                    color: overflowButton.down ? theme.pressed : "transparent"
                }

                // Three drawn dots rather than "⋮". U+22EE happens to be in
                // Roboto, so the character would survive — but the refresh
                // glyph beside it would not, and a toolbar where one icon is a
                // character and the other is a drawing is a toolbar where the
                // two never quite line up.
                //
                // The Item wrapper is load-bearing: a Column is a positioner,
                // so it accepts the 40px height Control gives it and then lays
                // its children out from y: 0 regardless. The dots sat against
                // the top of the button until this was anchored.
                contentItem: Item {
                    Column {
                        anchors.centerIn: parent
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
                }

                Menu {
                    id: overflow
                    y: overflowButton.height + 4
                    x: overflowButton.width - width
                    implicitWidth: 184
                    padding: 6

                    background: Rectangle {
                        color: theme.sheet
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
                        text: "Privacy policy"
                        onTriggered: Qt.openUrlExternally(window.privacyPolicyUrl)
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

                SummaryView {
                    onOpenTab: (index) => window.currentPage = index
                }
                ChapelView {}
                DiningView {}
                ChucksView {}
            }

            // ---- Bottom bar --------------------------------------------
            Rectangle {
                id: bottomBar
                Layout.fillWidth: true
                // Grows by the gesture-handle inset when it is the bottom-most
                // thing on screen; see "Edge to edge" at the top.
                implicitHeight: 60 + bottomBar.SafeArea.margins.bottom
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
                    anchors.bottomMargin: bottomBar.SafeArea.margins.bottom
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

                    NavButton {
                        Layout.fillWidth: true
                        text: "Chucks"
                        kind: "chucks"
                        selected: window.currentPage === 3
                        onClicked: window.currentPage = 3
                    }
                }
            }
        }

        // The web surface. Kept loaded at all times — it holds the session, and
        // unloading it would throw away the cookie jar on Android.
        Item {
            id: surfacePage

            Loader {
                id: surfaceLoader
                anchors.fill: parent
                // Microsoft's sign-in page must not sit under the gesture
                // handle. Zero whenever the status footer is showing below it.
                anchors.bottomMargin: surfacePage.SafeArea.margins.bottom
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
        id: statusFooter
        visible: login.status.length > 0
        implicitHeight: visible
                        ? statusLabel.implicitHeight + 18 + statusFooter.SafeArea.margins.bottom
                        : 0
        color: theme.ribbon

        Label {
            id: statusLabel
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 9
            anchors.leftMargin: 14 + statusFooter.SafeArea.margins.left
            anchors.rightMargin: 14 + statusFooter.SafeArea.margins.right
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
              + "happens on Microsoft's own page.\n\n"
              + "Privacy policy: in the menu, or at "
              + "github.com/KromaKobra/cedarview/blob/main/PRIVACY.md"
    }

    InfoSheet {
        id: settingsSheet
        heading: "Settings"
        body: "The app reads your own records on demand and keeps its session "
              + "in app-private storage. Sign out from the same menu to clear it."

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Label {
                    text: "Light theme"
                    color: theme.text
                    font.pixelSize: 14
                }

                Label {
                    Layout.fillWidth: true
                    text: "Dark is easier at 7am; light is easier outdoors."
                    color: theme.faint
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }

            // Driven from `settings`, not from the switch's own state: the
            // preference lives in QSettings and this is a view of it.
            ToggleSwitch {
                Layout.alignment: Qt.AlignVCenter
                on: settings.lightMode
                onClicked: settings.toggleLightMode()
            }
        }
    }
}
