// A rounded panel on the page. Most of every screen sits in one of these.
//
// Sizes itself to its content: put a ColumnLayout in it and the card's height
// follows. That is the whole reason this exists as a component — a screen of
// Rectangles each carrying its own `implicitHeight: something + 32` is a
// screen where one forgotten +32 clips a line of text off the bottom.
//
// Depth without a second colour: a soft shadow under it in the light theme, a
// faint highlight along its top edge in the dark one.
//
// Optionally tappable: set `tappable` and handle `tapped`. A TapHandler rather
// than a MouseArea, so a drag that starts on a card still scrolls the page (and
// still pulls to refresh) instead of being swallowed as a press.

import QtQuick
import QtQuick.Layouts

Item {
    id: card

    default property alias content: inner.data
    property real padding: 16
    property real topPadding: padding
    property real bottomPadding: padding
    property real radius: theme.cardRadius
    property color color: theme.surface
    property bool tappable: false

    signal tapped()

    Theme { id: theme }

    Layout.fillWidth: true
    implicitHeight: inner.implicitHeight + topPadding + bottomPadding

    // The shadow: two offset layers of the same hue, which reads as a soft
    // drop at a fraction of what a blur would cost per card.
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 2
        anchors.bottomMargin: -2
        radius: card.radius
        color: theme.cardShadow
    }
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 6
        anchors.bottomMargin: -6
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        radius: card.radius
        color: theme.cardShadow
        opacity: 0.6
    }

    Rectangle {
        id: face
        anchors.fill: parent
        radius: card.radius
        color: card.color
        border.width: theme.light ? 0 : 1
        border.color: theme.cardHighlight

        Behavior on color { ColorAnimation { duration: 180 } }

        // The press highlight, under the content.
        Rectangle {
            anchors.fill: parent
            radius: card.radius
            color: tap.pressed ? theme.pressed : "transparent"
        }
    }

    TapHandler {
        id: tap
        enabled: card.tappable
        onTapped: card.tapped()
    }

    ColumnLayout {
        id: inner
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: card.padding
        anchors.rightMargin: card.padding
        anchors.topMargin: card.topPadding
        spacing: 0
    }
}
