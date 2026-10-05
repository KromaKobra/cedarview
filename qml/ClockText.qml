// "10:00 AM" set the way the design sets times: the figures in the display
// face, the AM or PM after them smaller and quieter.

import QtQuick

Row {
    id: clock

    // "10:00 AM" — split at the space.
    property string text
    property color color: theme.text
    property color meridiemColor: theme.faint
    property real size: 16

    Theme { id: theme }

    readonly property var parts: text.split(" ")

    spacing: 3

    Text {
        id: figures
        text: clock.parts[0] || ""
        color: clock.color
        font.family: theme.display
        font.pixelSize: clock.size
        font.weight: Font.Bold
    }
    Text {
        anchors.baseline: figures.baseline
        visible: text.length > 0
        text: clock.parts.length > 1 ? clock.parts[1] : ""
        color: clock.meridiemColor
        font.family: theme.ui
        font.pixelSize: Math.round(clock.size * 0.68)
        font.weight: Font.DemiBold
    }
}
