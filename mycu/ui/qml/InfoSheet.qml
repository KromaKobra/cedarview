// A modal note — About, and the settings placeholder.
//
// A Popup we dress ourselves rather than a Dialog: Dialog's standardButtons are
// drawn by the Controls style, which is light, and a white OK button on a black
// sheet is the one thing that would give away that the rest of this is a theme
// painted over a default.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: sheet

    property string heading
    property string body

    Theme { id: theme }

    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 48 : 320, 340)
    modal: true
    focus: true
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: "#1A1A1F"
        radius: 18
        border.width: 1
        border.color: theme.cardBorder
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.6)
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
            text: sheet.body
            color: theme.muted
            font.pixelSize: 13
            lineHeight: 1.25
            wrapMode: Text.Wrap
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
                color: close.down ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.07)
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
