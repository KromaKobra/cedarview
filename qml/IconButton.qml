// A square button with one icon: search and More in the header, close on a
// sheet. 44 pixels — the smallest a thumb reliably hits. `flat` drops the
// surface behind it, for a back arrow in a bar.

import QtQuick
import QtQuick.Controls

AbstractButton {
    id: button

    property string glyph
    property color iconColor: theme.text
    property color fill: theme.surface
    property bool flat: false
    property real iconSize: 20
    property real radius: 16

    Theme { id: theme }

    implicitWidth: 44
    implicitHeight: 44
    focusPolicy: Qt.NoFocus
    Accessible.role: Accessible.Button
    Accessible.name: text

    background: Rectangle {
        radius: button.radius
        color: button.flat ? (button.down ? theme.pressed : "transparent")
                           : button.down ? Qt.darker(button.fill, theme.light ? 1.04 : 0.9) : button.fill
        border.width: !button.flat && !theme.light ? 1 : 0
        border.color: theme.cardHighlight
    }

    contentItem: Item {
        Glyph {
            anchors.centerIn: parent
            width: button.iconSize
            height: button.iconSize
            kind: button.glyph
            color: button.iconColor
        }
    }
}
