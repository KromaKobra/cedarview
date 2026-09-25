// One destination in the bottom bar.
//
// The selected tab gets a tinted pill behind its icon as well as the accent
// colour, so which tab you are on survives being read at a glance, in sunlight,
// or by someone who cannot separate orange from grey.

import QtQuick
import QtQuick.Controls

AbstractButton {
    id: nav

    //: One of Glyph's kinds — "summary", "chapel", "dining", "chucks".
    property string kind
    property bool selected: false

    Theme { id: theme }

    implicitHeight: 58
    opacity: down ? 0.68 : 1.0

    contentItem: Column {
        anchors.centerIn: parent
        spacing: 4

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 46
            height: 28
            radius: 14
            color: nav.selected ? theme.cedarSoft : "transparent"

            Glyph {
                anchors.centerIn: parent
                width: 17
                height: 17
                kind: nav.kind
                color: nav.selected ? theme.cedar : theme.faint
            }
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: nav.text
            color: nav.selected ? theme.text : theme.faint
            font.pixelSize: 10
            font.bold: nav.selected
        }
    }

    background: Item {}
}
