// The thin bar under the header while anything is being fetched. Quiet on
// purpose: the figures on screen are already there (from the last run), so a
// refresh is a background matter, not a spinner over everything.

import QtQuick

Item {
    id: bar

    property bool running: false

    Theme { id: theme }

    implicitHeight: 3
    clip: true
    opacity: running ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 250 } }

    Rectangle {
        id: dash
        height: parent.height
        width: bar.width * 0.35
        radius: height / 2
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: theme.cedar }
            GradientStop { position: 1.0; color: "transparent" }
        }

        NumberAnimation on x {
            from: -dash.width
            to: bar.width
            duration: 1100
            loops: Animation.Infinite
            running: bar.opacity > 0
            easing.type: Easing.InOutCubic
        }
    }
}
