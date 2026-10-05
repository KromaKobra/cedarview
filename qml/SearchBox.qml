// A search box: a field in a rounded frame with a magnifier — Search's and
// the Campus tab's. Drawn the same in every Controls style, which on the phone
// is Material:
//
// * its own placeholder, not TextField's. Material's floats up onto the top
//   edge once the field has focus or text — onto this frame's border, which
//   it was never drawn to sit on;
// * its own padding and vertical alignment, since the style's are sized for
//   the style's frame.
//
// Children go in the field, like the clear button on Search's.
//
// Not "SearchField": Qt Quick Controls has one of those since 6.10, and a
// file that imports QtQuick.Controls gets that one, not ours.

import QtQuick
import QtQuick.Controls

TextField {
    id: field

    property string placeholder
    property color glyphColor: theme.muted
    // Search's own page: larger, bolder text and a heavier focus ring.
    property bool emphasised: false

    // Give up the keyboard and the caret. Hiding the field does not: an
    // invisible item keeps its focus, and on Android so does its cursor
    // handle, left floating over whatever is on screen next.
    function release() {
        focus = false
        Qt.inputMethod.hide()
    }

    Theme { id: theme }

    implicitHeight: 48
    leftPadding: 42
    rightPadding: 16
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    color: theme.text
    font.family: theme.ui
    font.pixelSize: emphasised ? 16 : 15
    font.weight: emphasised ? Font.DemiBold : Font.Normal
    inputMethodHints: Qt.ImhNoPredictiveText
    Accessible.description: placeholder

    background: Rectangle {
        radius: 16
        color: theme.surface
        border.width: field.emphasised ? 2 : 1
        border.color: field.activeFocus ? theme.cedar : theme.line
    }

    Glyph {
        x: 14
        anchors.verticalCenter: parent.verticalCenter
        width: 18
        height: 18
        kind: "search"
        color: field.glyphColor
    }

    Text {
        x: field.leftPadding
        width: field.width - field.leftPadding - field.rightPadding
        anchors.verticalCenter: parent.verticalCenter
        // Gone with the first letter — including one the keyboard is still
        // composing, which is not in `text` yet.
        visible: field.length === 0 && field.preeditText.length === 0
        text: field.placeholder
        color: theme.faint
        font: field.font
        elide: Text.ElideRight
    }
}
