// A menu row in the app's own colours.
//
// Qt Quick Controls' default Menu is light, and `Menu.delegate` only applies to
// items built from a model — MenuItems written out by hand, which is what an
// overflow menu is, keep the stock style. So each row brings its own.

import QtQuick
import QtQuick.Controls

MenuItem {
    id: item

    Theme { id: theme }

    implicitHeight: 42

    contentItem: Label {
        leftPadding: 10
        text: item.text
        color: item.enabled ? theme.text : theme.faint
        font.pixelSize: 14
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: 9
        color: item.highlighted || item.down
               ? Qt.rgba(1, 1, 1, 0.07)
               : "transparent"
    }
}
