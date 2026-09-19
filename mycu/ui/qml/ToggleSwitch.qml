// An on/off switch, drawn rather than imported.
//
// Qt Quick Controls has a perfectly good `Switch`, and it is not used here for
// the reason DarkMenuItem.qml and InfoSheet.qml already record: the Basic
// style's controls arrive with their own colours, and one stock widget on a
// sheet the app has painted itself is more conspicuous than a whole stock
// dialog would be. Restyling Switch means replacing its `indicator` anyway, at
// which point this is the smaller thing.
//
// It exposes `on` rather than reusing AbstractButton's `checked`, and it never
// writes to it. `checked` is self-toggling: binding it to where the setting
// really lives and *also* letting the click change it destroys the binding on
// the first press, after which the switch and the setting are two facts that
// only happen to agree. So the caller binds `on` and handles `clicked`, and the
// switch moves when — and only when — the setting does.

import QtQuick
import QtQuick.Controls

AbstractButton {
    id: control

    //: Bind this. Do not assign to it from inside.
    property bool on: false

    Theme { id: theme }

    implicitWidth: 46
    implicitHeight: 28

    background: Item {}

    contentItem: Item {
        Rectangle {
            id: track
            anchors.centerIn: parent
            width: 44
            height: 26
            radius: height / 2
            color: control.on ? theme.accent : theme.track
            opacity: control.down ? 0.75 : 1.0

            // The colour change is the state; animating it stops the switch
            // from reading as two different controls.
            Behavior on color {
                ColorAnimation { duration: 140 }
            }

            Rectangle {
                // Off sits left, on sits right. The knob is the sheet's own
                // colour, so it reads as a hole in the track rather than as a
                // white dot that happens to work in both themes.
                x: control.on ? track.width - width - 3 : 3
                anchors.verticalCenter: parent.verticalCenter
                width: 20
                height: 20
                radius: height / 2
                color: theme.sheet

                Behavior on x {
                    NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
                }
            }
        }
    }
}
