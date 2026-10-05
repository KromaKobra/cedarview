// A row of equal segments in a recessed track, the chosen one raised: the
// Dining sections, the menu's sittings, a day-type filter. Set `width` (or let
// a layout fill it); the segments share it evenly. `sublabels`, when given,
// go under the labels — the menu's sittings carry their hours.

import QtQuick
import QtQuick.Controls

Item {
    id: control

    property var labels: []
    property var sublabels: []
    property int currentIndex: 0
    property real fontSize: 13.5
    property real radius: 16

    signal activated(int index)

    Theme { id: theme }

    readonly property real inset: 4
    readonly property real segment: labels.length > 0
                                    ? (width - inset * 2 - inset * (labels.length - 1)) / labels.length : 0

    implicitHeight: sublabels.length > 0 ? 62 : 46

    Rectangle {
        anchors.fill: parent
        radius: control.radius
        color: theme.surface2
    }

    Rectangle {
        visible: control.labels.length > 0 && control.currentIndex >= 0
        x: control.inset + (control.segment + control.inset) * control.currentIndex
        y: control.inset
        width: control.segment
        height: control.height - control.inset * 2
        radius: control.radius - 4
        color: theme.raised
        border.width: theme.light ? 0 : 1
        border.color: theme.cardHighlight

        // A hairline shadow under the raised segment.
        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.topMargin: 1
            anchors.bottomMargin: -1
            radius: parent.radius
            color: Qt.rgba(0, 0, 0, theme.light ? 0.08 : 0.25)
        }

        Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    }

    Row {
        anchors.fill: parent
        anchors.margins: control.inset
        spacing: control.inset

        Repeater {
            model: control.labels

            AbstractButton {
                id: segmentButton
                required property int index
                required property var modelData
                readonly property bool active: control.currentIndex === index
                width: control.segment
                height: parent.height
                padding: 0
                focusPolicy: Qt.NoFocus
                onClicked: {
                    if (!active)
                        control.activated(index)
                }
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData
                Accessible.selected: active
                background: Item {}
                // Centred by anchors. It used to be a Column padded by half of
                // (height − implicitHeight), but a Column's implicitHeight
                // counts its own padding, so the label settled off centre.
                contentItem: Item {
                    Column {
                        anchors.centerIn: parent
                        spacing: 2

                        Text {
                            width: segmentButton.width
                            text: segmentButton.modelData
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            color: segmentButton.active ? theme.text : theme.muted
                            opacity: segmentButton.down && !segmentButton.active ? 0.6 : 1.0
                            font.family: theme.ui
                            font.pixelSize: control.fontSize
                            font.weight: segmentButton.active ? Font.Bold : Font.DemiBold
                        }
                        Text {
                            visible: control.sublabels.length > segmentButton.index
                            width: segmentButton.width
                            text: visible ? control.sublabels[segmentButton.index] : ""
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            color: segmentButton.active ? theme.gold : theme.faint
                            font.family: theme.display
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }
    }
}
