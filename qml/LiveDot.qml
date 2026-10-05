// The gold dot that says "this is happening now, or about to": a dot in a
// soft halo of its own colour.

import QtQuick

Item {
    id: dot

    property color color: theme.goldHero

    Theme { id: theme }

    implicitWidth: 16
    implicitHeight: 16

    Rectangle {
        anchors.centerIn: parent
        width: 16
        height: 16
        radius: 8
        color: dot.color
        opacity: 0.2
    }
    Rectangle {
        anchors.centerIn: parent
        width: 8
        height: 8
        radius: 4
        color: dot.color
    }
}
