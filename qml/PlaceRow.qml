// A place in a list: an icon (or the building's code) in a tile, its name,
// one line under it, and how long it has left in a chip. "Still open" on
// Today, and search's Places.

import QtQuick
import QtQuick.Layouts

Item {
    id: row

    property string name
    property string detail
    property string badge
    // The badge in gold: closing soon, open now.
    property bool hot: false
    // A Glyph kind for the tile; `code` instead puts the building's letters
    // there.
    property string icon
    property string code
    // The tile in gold rather than blue.
    property bool iconHot: false
    property bool divider: false

    signal tapped()

    Theme { id: theme }

    Layout.fillWidth: true
    implicitHeight: layout.implicitHeight + 24

    Rectangle {
        visible: row.divider
        width: parent.width
        height: 1
        color: theme.line
    }

    TapHandler {
        id: tap
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
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12

        Rectangle {
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            radius: 14
            color: row.iconHot ? theme.goldSoft : row.code.length > 0 ? theme.surface2 : theme.cedarSoft

            Glyph {
                visible: row.code.length === 0
                anchors.centerIn: parent
                width: 18
                height: 18
                kind: row.icon
                color: row.iconHot ? theme.gold : theme.cedar
            }
            Text {
                visible: row.code.length > 0
                anchors.centerIn: parent
                text: row.code
                color: row.iconHot ? theme.gold : theme.text
                font.family: theme.display
                font.pixelSize: 13
                font.weight: Font.ExtraBold
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: row.name
                color: theme.text
                font.family: theme.ui
                font.pixelSize: 15
                font.weight: Font.Bold
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: row.detail
                color: theme.muted
                font.family: theme.ui
                font.pixelSize: 13
                elide: Text.ElideRight
            }
        }

        Chip {
            visible: row.badge.length > 0
            text: row.badge
            textColor: row.hot ? theme.gold : theme.muted
            fill: row.hot ? theme.goldSoft : theme.surface2
        }
    }
}
