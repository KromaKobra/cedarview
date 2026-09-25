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
    bottomPadding: 0

    readonly property string privacyPolicyUrl:
        "https://github.com/KromaKobra/cedarview/blob/main/PRIVACY.md"
    readonly property var pageTitles: ["Today", "Chapel", "Meal card", "Home Cooking"]
    readonly property var pageSubtitles: [
        "Your Cedarville at a glance",
        "Attendance and upcoming speakers",
        "Balances and recent activity",
        "Menus by day"
    ]

    Theme { id: theme }

    property bool showingLogin: login.surfaceVisible
    property int currentPage: 0
    property bool busy: chapel.busy || dining.busy

    function refreshEverything() {
        chapel.refreshAll()
        dining.refreshAll()
        semester.refreshAll()
    }

    header: Rectangle {
        id: appHeader
        implicitHeight: 70 + appHeader.SafeArea.margins.top
        color: theme.ribbon

        RowLayout {
            anchors.fill: parent
            anchors.topMargin: appHeader.SafeArea.margins.top
            anchors.leftMargin: 16 + appHeader.SafeArea.margins.left
            anchors.rightMargin: 8 + appHeader.SafeArea.margins.right
            spacing: 11

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: 38
                Layout.preferredHeight: 38
                radius: 13
                color: theme.cedarSoft

                Image {
                    anchors.centerIn: parent
                    width: 28
                    height: 28
                    source: "icon.png"
                    sourceSize: Qt.size(56, 56)
                    fillMode: Image.PreserveAspectFit
                    mipmap: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 1

                Label {
                    Layout.fillWidth: true
                    text: window.pageTitles[window.currentPage]
                    color: theme.text
                    font.pixelSize: 18
                    font.bold: true
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    text: window.pageSubtitles[window.currentPage]
                    color: theme.muted
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }

            AbstractButton {
                id: refreshButton
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 40
                implicitHeight: 40
                enabled: !window.showingLogin && !window.busy
                onClicked: window.refreshEverything()

                background: Rectangle {
                    radius: 14
                    color: refreshButton.down ? theme.pressedStrong : theme.cardAlt
                }

                contentItem: Item {
                    BusyIndicator {
                        anchors.centerIn: parent
                        visible: window.busy
                        running: visible
                        implicitWidth: 20
                        implicitHeight: 20
                    }

                    Glyph {
                        anchors.centerIn: parent
                        visible: !window.busy
                        kind: "refresh"
                        color: refreshButton.enabled ? theme.cedar : theme.faint
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
                    radius: 14
                    color: overflowButton.down ? theme.pressedStrong : theme.cardAlt
                }

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
                                radius: 2
                                color: theme.muted
                            }
                        }
                    }
                }

                Menu {
                    id: overflow
                    y: overflowButton.height + 6
                    x: overflowButton.width - width
                    implicitWidth: 190
                    padding: 7

                    background: Rectangle {
                        color: theme.sheet
                        radius: 17
                        border.width: 1
                        border.color: theme.cardBorder
                    }

                    DarkMenuItem { text: "Appearance"; onTriggered: settingsSheet.open() }
                    DarkMenuItem { text: "About CedarView"; onTriggered: aboutSheet.open() }
                    DarkMenuItem {
                        text: "Privacy policy"
                        onTriggered: Qt.openUrlExternally(window.privacyPolicyUrl)
                    }
                    DarkMenuItem { text: "Sign out"; onTriggered: login.signOut() }
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

        Item {
            // Quiet ambient shapes make the space feel dimensional without
            // competing with the data or adding image assets.
            Rectangle {
                x: -80
                y: -90
                width: 250
                height: 250
                radius: 125
                color: theme.cedarSoft
                opacity: 0.22
            }

            Rectangle {
                x: parent.width - 105
                y: parent.height * 0.46
                width: 180
                height: 180
                radius: 90
                color: theme.accentSoft
                opacity: 0.16
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                SwipeView {
                    id: pages
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: window.currentPage
                    onCurrentIndexChanged: window.currentPage = currentIndex

                    SummaryView { onOpenTab: (index) => window.currentPage = index }
                    ChapelView {}
                    DiningView {}
                    ChucksView {}
                }

                Rectangle {
                    id: bottomBar
                    Layout.fillWidth: true
                    implicitHeight: 68 + bottomBar.SafeArea.margins.bottom
                    color: theme.nav

                    Rectangle {
                        anchors.top: parent.top
                        width: parent.width
                        height: 1
                        color: theme.divider
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 7
                        anchors.rightMargin: 7
                        anchors.topMargin: 4
                        anchors.bottomMargin: bottomBar.SafeArea.margins.bottom + 2
                        spacing: 0

                        NavButton {
                            Layout.fillWidth: true
                            text: "Today"
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
                            text: "Menu"
                            kind: "chucks"
                            selected: window.currentPage === 3
                            onClicked: window.currentPage = 3
                        }
                    }
                }
            }
        }

        Item {
            id: surfacePage

            Loader {
                id: surfaceLoader
                anchors.fill: parent
                anchors.bottomMargin: surfacePage.SafeArea.margins.bottom
                source: platformSurface
                asynchronous: false

                onLoaded: {
                    bridge.attachSurface(item)
                    item.currentUrlChanged.connect(function () {
                        login.onUrlChanged(item.currentUrl)
                    })
                    login.begin(bridge.startPath)
                }

                Connections {
                    target: login
                    function onNavigateRequested(url) {
                        if (surfaceLoader.item)
                            surfaceLoader.item.navigate(url)
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: surfaceLoader.status === Loader.Error
                width: parent.width - 48
                text: "The secure sign-in window could not be opened.\n\n"
                      + "Please close CedarView and try again."
                color: theme.muted
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
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
            anchors.leftMargin: 16 + statusFooter.SafeArea.margins.left
            anchors.rightMargin: 16 + statusFooter.SafeArea.margins.right
            text: login.status
            color: theme.muted
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
    }

    InfoSheet {
        id: aboutSheet
        heading: "CedarView"
        body: "The useful parts of myCU, gathered into one calm view.\n\n"
              + "Backend: " + bridge.platformName + "\n\n"
              + "Your password is never seen or stored by CedarView. Sign-in happens "
              + "on Microsoft's own page, and your records remain on this device."
    }

    InfoSheet {
        id: settingsSheet
        heading: "Appearance"
        body: "Choose the palette that is most comfortable where you are."

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { text: "Light theme"; color: theme.text; font.pixelSize: 14; font.bold: true }
                Label {
                    Layout.fillWidth: true
                    text: "A brighter palette for daylight."
                    color: theme.faint
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }

            ToggleSwitch {
                Layout.alignment: Qt.AlignVCenter
                on: settings.lightMode
                onClicked: settings.toggleLightMode()
            }
        }
    }
}
