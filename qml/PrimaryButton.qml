// The one gold button on a screen: "Sign in with Cedarville", "Watch live".
// Lit from above like the gold figures, with a warm glow under it. `quiet`
// is the plain text button that sits beneath it.

import QtQuick
import QtQuick.Controls
import QtQuick.Effects

AbstractButton {
    id: button

    property string glyph
    property bool quiet: false
    // A quiet button on navy draws in navyText.
    property bool navy: false

    Theme { id: theme }

    implicitHeight: quiet ? 48 : 56
    implicitWidth: label.implicitWidth + 48
    focusPolicy: Qt.NoFocus
    Accessible.role: Accessible.Button
    Accessible.name: text

    background: Item {
        RectangularShadow {
            visible: !button.quiet
            anchors.fill: face
            offset.y: 6
            blur: 22
            radius: face.radius
            color: Qt.rgba(0.99, 0.72, 0.07, 0.28)
        }
        Rectangle {
            id: face
            anchors.fill: parent
            visible: !button.quiet
            radius: 18
            gradient: Gradient {
                GradientStop { position: 0.0; color: theme.buttonA }
                GradientStop { position: 1.0; color: theme.buttonB }
            }
            opacity: button.down ? 0.85 : 1
        }
        Rectangle {
            anchors.fill: parent
            visible: button.quiet && button.down
            radius: 18
            color: button.navy ? Qt.rgba(1, 1, 1, 0.08) : theme.pressed
        }
    }

    contentItem: Item {
        Row {
            anchors.centerIn: parent
            spacing: 8

            Glyph {
                visible: button.glyph.length > 0
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                kind: button.glyph
                color: label.color
            }

            Text {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                text: button.text
                color: button.quiet ? (button.navy ? theme.navyText : theme.cedar) : theme.buttonText
                font.family: theme.ui
                font.pixelSize: button.quiet ? 14.5 : 16
                font.weight: button.quiet ? Font.DemiBold : Font.Bold
            }
        }
    }
}
