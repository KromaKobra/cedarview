// One line of a day: the time in a column of its own, a ring on a thread, and
// what happens then. Rest of today's timeline is a card of these.

import QtQuick
import QtQuick.Layouts

Item {
    id: row

    property string time
    property string meridiem
    property string title
    // "in 48 min", on the next thing only.
    property string chip
    property string detail
    // Under a meal: "Garden Bites: Broccoli Alfredo", with a leaf.
    property string extra
    // The ring: "gold" for what is next, "cedar" for later meals, else faint.
    property string accent: "faint"
    // The last row has no thread running on below it.
    property bool last: false
    property bool tappable: true

    signal tapped()

    Theme { id: theme }

    Layout.fillWidth: true
    implicitHeight: layout.implicitHeight

    readonly property color ringColor: accent === "gold" ? theme.gold : accent === "cedar" ? theme.cedar : theme.faint

    TapHandler {
        id: tap
        enabled: row.tappable
        onTapped: row.tapped()
    }

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: -8
        anchors.rightMargin: -8
        radius: 12
        color: tap.pressed ? theme.pressed : "transparent"
    }

    RowLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 12

        ColumnLayout {
            Layout.preferredWidth: 50
            Layout.alignment: Qt.AlignTop
            spacing: 0

            Text {
                text: row.time
                color: theme.text
                font.family: theme.display
                font.pixelSize: 17
                font.weight: Font.Bold
            }
            Text {
                text: row.meridiem
                color: theme.faint
                font.family: theme.ui
                font.pixelSize: 11
                font.weight: Font.Bold
                font.letterSpacing: 0.8
            }
        }

        // The ring, and the thread down to the next one.
        Item {
            Layout.preferredWidth: 12
            Layout.fillHeight: true

            Rectangle {
                id: ring
                y: 5
                width: 12
                height: 12
                radius: 6
                color: "transparent"
                border.width: 2.5
                border.color: row.ringColor
            }
            Rectangle {
                visible: !row.last
                anchors.horizontalCenter: ring.horizontalCenter
                anchors.top: ring.bottom
                anchors.topMargin: 4
                anchors.bottom: parent.bottom
                width: 2
                radius: 1
                color: theme.line
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            Layout.bottomMargin: row.last ? 10 : 16
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: !chipItem.visible
                    text: row.title
                    color: theme.text
                    font.family: theme.ui
                    font.pixelSize: 15
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                Chip {
                    id: chipItem
                    visible: row.chip.length > 0
                    text: row.chip
                    implicitHeight: 22
                    fontSize: 11.5
                    textColor: theme.gold
                    fill: theme.goldSoft
                }
                Item { Layout.fillWidth: chipItem.visible }
            }

            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: row.detail
                color: theme.muted
                font.family: theme.ui
                font.pixelSize: 13
                lineHeight: 1.15
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }

            RowLayout {
                visible: row.extra.length > 0
                Layout.fillWidth: true
                Layout.topMargin: 1
                spacing: 5

                Glyph {
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    kind: "leaf"
                    color: theme.cedar
                }
                Text {
                    Layout.fillWidth: true
                    text: row.extra
                    color: theme.faint
                    font.family: theme.ui
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }
    }
}
