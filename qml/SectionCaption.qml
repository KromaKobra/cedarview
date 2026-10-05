// A section's small capitals, with a note on the right: "REST OF TODAY ·
// Now 9:42 AM". Sits between cards, never inside one.

import QtQuick
import QtQuick.Layouts

RowLayout {
    id: caption

    property string text
    property string note
    // An icon before the caption, in `iconColor` ("Your places" has a star).
    property string icon
    property color iconColor: theme.gold

    Theme { id: theme }

    Layout.fillWidth: true
    Layout.topMargin: 6
    Layout.leftMargin: 4
    Layout.rightMargin: 4
    spacing: 7

    Glyph {
        visible: caption.icon.length > 0
        Layout.preferredWidth: 15
        Layout.preferredHeight: 15
        kind: caption.icon
        color: caption.iconColor
    }

    Text {
        Layout.fillWidth: true
        text: caption.text.toUpperCase()
        color: theme.faint
        font.family: theme.ui
        font.pixelSize: 11
        font.weight: Font.Bold
        font.letterSpacing: 1.1
        elide: Text.ElideRight
    }

    Text {
        visible: text.length > 0
        text: caption.note
        color: theme.faint
        font.family: theme.ui
        font.pixelSize: 12
    }
}
