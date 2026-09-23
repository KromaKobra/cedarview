// A menu row in the app's own colours.
//
// Qt Quick Controls' default Menu carries the Basic style's palette, and
// `Menu.delegate` only applies to items built from a model — MenuItems written
// out by hand, which is what an overflow menu is, keep the stock style. So each
// row brings its own.
//
// The name is now half a lie: since Theme.qml grew a light palette this draws
// whichever one is on. Left as-is: a rename touches the module's file list in
// CMakeLists.txt and every file that uses the type, for a better filename and
// nothing else.

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
        color: item.highlighted || item.down ? theme.pressed : "transparent"
    }
}
