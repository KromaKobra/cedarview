// One destination in the bottom bar.
//
// The selected tab gets a tinted pill behind its icon as well as the accent
// colour, so which tab you are on survives being read at a glance, in sunlight,
// or by someone who cannot separate orange from grey.

import QtQuick
import QtQuick.Controls

AbstractButton {
    id: nav

    //: One of Glyph's kinds — "summary", "chapel", "dining".
    property string kind
    property bool selected: false

    Theme { id: theme }

    implicitHeight: 52
    opacity: down ? 0.6 : 1.0

    contentItem: Column {
        spacing: 5

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 42
            height: 26
            radius: 13
            color: nav.selected ? theme.accentSoft : "transparent"

            Glyph {
                anchors.centerIn: parent
                width: 18
                height: 18
                kind: nav.kind
                color: nav.selected ? theme.accent : theme.faint
            }
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: nav.text
            color: nav.selected ? theme.accent : theme.faint
            font.pixelSize: 11
            font.bold: nav.selected
        }
    }

    background: Item {}
}
