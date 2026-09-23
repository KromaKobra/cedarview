// A modal note — About, and Settings.
//
// A Popup we dress ourselves rather than a Dialog: Dialog's standardButtons are
// drawn by the Controls style, which is light, and a stock OK button on a sheet
// the app has painted itself is the one thing that would give away that the
// rest of this is a theme over a default. (That reasoning long predates the
// light theme, and survives it — "light" here still means the Basic style's
// colours, not ours.)
//
// Anything a caller nests inside lands between the body text and the Close
// button, which is how the Settings sheet gets its toggle without needing a
// second copy of this chrome.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: sheet

    property string heading
    property string body

    //: The default property, so a caller writes `InfoSheet { RowLayout { … } }`.
    //: Same shape as Card.qml, for the same reason.
    default property alias extras: extraArea.data

    Theme { id: theme }

    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 48 : 320, 340)
    modal: true
    focus: true
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: theme.sheet
        radius: 18
        border.width: 1
        border.color: theme.cardBorder
    }

    Overlay.modal: Rectangle {
        color: theme.scrim
    }

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            Layout.fillWidth: true
            text: sheet.heading
            color: theme.text
            font.pixelSize: 18
            font.bold: true
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: sheet.body
            color: theme.muted
            font.pixelSize: 13
            lineHeight: 1.25
            wrapMode: Text.Wrap
        }

        // Where a caller's children land. Zero-height and invisible in the
        // sheets that do not use it, so About is unchanged.
        ColumnLayout {
            id: extraArea
            Layout.fillWidth: true
            Layout.topMargin: children.length > 0 ? 4 : 0
            spacing: 12
        }

        AbstractButton {
            id: close
            Layout.alignment: Qt.AlignRight
            Layout.topMargin: 4
            implicitWidth: 84
            implicitHeight: 36
            onClicked: sheet.close()

            background: Rectangle {
                radius: 10
                color: close.down ? theme.pressedStrong : theme.pressed
            }

            contentItem: Label {
                text: "Close"
                color: theme.text
                font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
