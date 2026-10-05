// One destination in the bottom bar.
//
// The selected tab gets a tinted pill behind its icon as well as the brand
// blue, so which tab you are on survives being read at a glance, in
// sunlight, or by someone who cannot separate the blue from grey.

import QtQuick
import QtQuick.Controls

AbstractButton {
    id: nav

    //: One of Glyph's kinds — "today", "chapel", "dining", "campus".
    property string glyph
    property bool selected: false

    Theme { id: theme }

    implicitHeight: 62
    focusPolicy: Qt.NoFocus
    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.selected: selected

    contentItem: Item {
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 6
            spacing: 4

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 58
                height: 32
                radius: 16
                color: nav.selected ? theme.cedarSoft : nav.down ? theme.pressed : "transparent"
                Behavior on color { ColorAnimation { duration: 160 } }

                Glyph {
                    anchors.centerIn: parent
                    width: 20
                    height: 20
                    kind: nav.glyph
                    color: nav.selected ? theme.cedar : theme.muted
                }
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: nav.text
                color: nav.selected ? theme.text : theme.muted
                font.family: theme.ui
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
        }
    }

    background: Item {}
}
