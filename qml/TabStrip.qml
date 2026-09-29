// A row of plain text tabs over a hairline, with a coloured bar under the
// chosen one that shoots across when the choice changes.
//
// The bar's edges move separately: the leading edge leaves first and the
// trailing one catches up, so it stretches toward the new tab and settles
// under it. Set `width` (or let a layout fill it); the tabs share it evenly and
// the bar is as wide as the chosen label.

import QtQuick
import QtQuick.Controls

Item {
    id: control

    property var labels: []
    property int currentIndex: 0
    property color barColor: theme.cedar
    property int fontSize: 14

    signal activated(int index)

    Theme { id: theme }

    readonly property real segment: labels.length > 0 ? width / labels.length : 0
    readonly property real barHeight: 3

    implicitHeight: 44

    FontMetrics {
        id: metrics
        font.pixelSize: control.fontSize
        font.weight: Font.DemiBold
    }

    // Where the bar's edges should end up: centred under the chosen label.
    function labelWidth() {
        return Math.min(metrics.advanceWidth(labels[currentIndex] || ""), segment)
    }
    function targetLeft() {
        return segment * currentIndex + (segment - labelWidth()) / 2
    }
    function targetRight() {
        return targetLeft() + labelWidth()
    }

    property real barLeft: 0
    property real barRight: 0
    property int shownIndex: 0
    property bool movingRight: true
    property bool animate: false

    // A resize or new labels jump the bar into place; only a tab change slides.
    function snap() {
        animate = false
        barLeft = targetLeft()
        barRight = targetRight()
        shownIndex = currentIndex
        animate = true
    }

    onCurrentIndexChanged: {
        movingRight = currentIndex > shownIndex
        shownIndex = currentIndex
        barLeft = targetLeft()
        barRight = targetRight()
    }
    onWidthChanged: snap()
    onLabelsChanged: snap()
    onFontSizeChanged: snap()
    Component.onCompleted: snap()

    Behavior on barLeft {
        enabled: control.animate
        NumberAnimation {
            duration: control.movingRight ? 340 : 210
            easing.type: Easing.OutQuint
        }
    }
    Behavior on barRight {
        enabled: control.animate
        NumberAnimation {
            duration: control.movingRight ? 210 : 340
            easing.type: Easing.OutQuint
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: theme.divider
    }

    Rectangle {
        visible: control.labels.length > 0
        x: control.barLeft
        y: control.height - height
        width: Math.max(0, control.barRight - control.barLeft)
        height: control.barHeight
        radius: height / 2
        color: control.barColor
    }

    Row {
        anchors.fill: parent
        anchors.bottomMargin: control.barHeight

        Repeater {
            model: control.labels

            AbstractButton {
                id: tabButton
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
                    color: tabButton.active ? theme.text : theme.muted
                    opacity: tabButton.down && !tabButton.active ? 0.6 : 1.0
                    font: metrics.font
                    Behavior on color { ColorAnimation { duration: 180 } }
                }
            }
        }
    }
}
