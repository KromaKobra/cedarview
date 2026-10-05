// A count shown as pips: one per skip, one per meal in the week, filled for
// each still left. Easier to read at a glance than a bar, because a pip is a
// thing you have.
//
// More than 30 and pips stop being countable (a 120-meal block plan), so it
// becomes one continuous bar of the same look.

import QtQuick

Item {
    id: meter

    property int total: 0
    property int filled: 0
    property real pipHeight: 8
    property real gap: 3
    // Lit from above: the first colour on top.
    property color colorA: theme.goldA
    property color colorB: theme.goldB
    property color offColor: theme.line

    Theme { id: theme }

    readonly property bool continuous: total > 30
    readonly property int shown: Math.max(0, Math.min(filled, total))

    implicitHeight: pipHeight
    implicitWidth: 200

    Row {
        visible: !meter.continuous && meter.total > 0
        anchors.fill: parent
        spacing: meter.gap

        Repeater {
            model: meter.continuous ? 0 : Math.max(0, meter.total)

            Rectangle {
                required property int index
                width: (meter.width - meter.gap * (meter.total - 1)) / meter.total
                height: meter.pipHeight
                radius: Math.min(4, height / 2)
                color: meter.offColor
                gradient: index < meter.shown ? lit : null

                Gradient {
                    id: lit
                    GradientStop { position: 0.0; color: meter.colorA }
                    GradientStop { position: 1.0; color: meter.colorB }
                }
            }
        }
    }

    Rectangle {
        visible: meter.continuous
        anchors.fill: parent
        radius: height / 2
        color: meter.offColor

        Rectangle {
            width: meter.total > 0 ? parent.width * meter.shown / meter.total : 0
            height: parent.height
            radius: height / 2
            gradient: Gradient {
                GradientStop { position: 0.0; color: meter.colorA }
                GradientStop { position: 1.0; color: meter.colorB }
            }
        }
    }
}
