// A row of equal segments with a raised thumb that slides to the chosen one.
//
// The same groove-and-thumb look as the Dining tab's All | Flex filter, for
// any number of text choices. Every choice stays visible, so it works as the
// Dining tab's section bar as well as a small filter. Set `width` (or let a
// layout fill it); the segments share it evenly.

import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Item {
    id: control

    property var labels: []
    property int currentIndex: 0
    // The label colour of the chosen segment.
    property color selectedColor: theme.text
    property int fontSize: 13

    signal activated(int index)

    Theme { id: theme }

    readonly property real inset: 3
    readonly property real segment: labels.length > 0 ? (width - inset * 2) / labels.length : 0

    implicitHeight: 38

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        border.width: 1
        border.color: theme.hairline
        gradient: Gradient {
            GradientStop { position: 0.0; color: theme.grooveTop }
            GradientStop { position: 1.0; color: theme.grooveBottom }
        }
    }

    Rectangle {
        visible: control.labels.length > 0
        x: control.inset + control.segment * control.currentIndex
        y: control.inset
        width: control.segment
        height: control.height - control.inset * 2
        radius: height / 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: theme.raisedTop }
            GradientStop { position: 1.0; color: theme.raisedBottom }
        }

        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: "#000000"
            shadowOpacity: theme.light ? 0.16 : 0.45
            shadowBlur: 0.45
            shadowVerticalOffset: 2
        }

        Behavior on x {
            NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
        }
    }

    Row {
        anchors.fill: parent
        anchors.margins: control.inset

        Repeater {
            model: control.labels

            AbstractButton {
                id: segmentButton
                readonly property bool active: control.currentIndex === index
                width: control.segment
                height: parent.height
                onClicked: {
                    if (!active)
                        control.activated(index)
                }
                Accessible.name: modelData
                background: Item {}
                contentItem: Label {
                    text: modelData
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    color: segmentButton.active ? control.selectedColor : theme.muted
                    opacity: segmentButton.down && !segmentButton.active ? 0.6 : 1.0
                    font.pixelSize: control.fontSize
                    font.bold: true
                    Behavior on color { ColorAnimation { duration: 180 } }
                }
            }
        }
    }
}
