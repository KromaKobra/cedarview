// Where a figure will be, while it loads: a bar in the surface's own second
// shade, with a band of light passing over it.
//
// Shown only while a source is loading and has nothing to show yet — the
// rule in sourcestatus.h. Set the size the real figure will roughly take, so
// nothing jumps when it arrives.

import QtQuick

Rectangle {
    id: skeleton

    // On a navy hero, a translucent white instead of the page's grey.
    property bool navy: false

    Theme { id: theme }

    implicitWidth: 80
    implicitHeight: 14
    radius: Math.min(height / 2, 8)
    color: navy ? theme.heroSkeleton : theme.surface2
    clip: true

    Rectangle {
        id: shine
        width: skeleton.width * 0.6
        height: skeleton.height
        rotation: 0
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: skeleton.navy ? Qt.rgba(1, 1, 1, 0.10)
                                                                   : theme.light ? Qt.rgba(1, 1, 1, 0.7)
                                                                                 : Qt.rgba(1, 1, 1, 0.05) }
            GradientStop { position: 1.0; color: "transparent" }
        }

        NumberAnimation on x {
            from: -shine.width
            to: skeleton.width
            duration: 1300
            loops: Animation.Infinite
            running: skeleton.visible
            easing.type: Easing.InOutQuad
        }
    }
}
