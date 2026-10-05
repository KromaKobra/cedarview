// Microsoft's sign-in page, inside CedarView's own chrome: a bar that says
// what this is and offers a way back, then the web surface below it, with a
// progress bar — and a spinner until the first page has painted, so there is
// never a blank white rectangle.
//
// The surface Loader lives here and stays instantiated whether or not this is
// showing: on the desktop the transport fetches through that same page, so it
// must stay attached even while hidden.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    // The loaded surface (WebSurface*.qml), for Main to wire up.
    readonly property alias surface: surfaceLoader.item
    readonly property alias surfaceStatus: surfaceLoader.status

    Theme { id: theme }

    color: theme.bg

    readonly property bool loading: surface ? surface.loading : true
    readonly property int progress: surface ? surface.loadProgress : 0
    // Becomes true once a page has finished loading since this was shown.
    property bool painted: false

    onVisibleChanged: painted = false
    onLoadingChanged: {
        if (!loading && visible)
            painted = true
    }
    // A page that never reports finishing still gets shown, eventually.
    Timer {
        interval: 8000
        running: root.visible && !root.painted
        onTriggered: root.painted = true
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: bar.implicitHeight + 20 + root.SafeArea.margins.top
            color: theme.surface

            RowLayout {
                id: bar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                anchors.leftMargin: 8
                anchors.rightMargin: 16
                spacing: 8

                IconButton {
                    glyph: "close"
                    flat: true
                    text: "Cancel sign-in"
                    onClicked: login.cancelSignIn()
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: "Sign in with your Cedarville account"
                        color: theme.text
                        font.family: theme.ui
                        font.pixelSize: 15
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        spacing: 5
                        Glyph {
                            Layout.preferredWidth: 13
                            Layout.preferredHeight: 13
                            kind: "lock"
                            stroke: 2
                            color: theme.faint
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "On Microsoft's page. CedarView never sees your password."
                            color: theme.faint
                            font.family: theme.ui
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            // The page's load progress, along the bar's bottom edge.
            Rectangle {
                anchors.bottom: parent.bottom
                height: 3
                width: parent.width * Math.max(0.08, root.progress / 100)
                visible: root.loading
                color: theme.cedar
                Behavior on width { NumberAnimation { duration: 200 } }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Loader {
                id: surfaceLoader
                anchors.fill: parent
                anchors.bottomMargin: root.SafeArea.margins.bottom
                source: platformSurface
                asynchronous: false
            }

            // Until the first page has painted, CedarView's own background and
            // a spinner rather than an empty white browser.
            Rectangle {
                anchors.fill: parent
                visible: !root.painted && surfaceLoader.status !== Loader.Error
                color: theme.bg

                BusyIndicator {
                    anchors.centerIn: parent
                    running: parent.visible
                }
            }

            Text {
                anchors.centerIn: parent
                visible: surfaceLoader.status === Loader.Error
                width: parent.width - 48
                text: "The secure sign-in window could not be opened.\n\nPlease close CedarView and try again."
                color: theme.muted
                font.family: theme.ui
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }
    }
}
