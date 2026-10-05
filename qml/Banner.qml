// A notice above the tabs: you're offline, your session ended, this is
// sample data. One line of what, one of why, and the one thing to do about
// it. Never instead of the data — over it, with the data still there.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: banner

    property string icon: "info"
    property string title
    property string detail
    property string actionText
    // A second, quieter action ("Exit" beside "Sign in").
    property string secondaryText

    signal action()
    signal secondaryAction()

    Theme { id: theme }

    Layout.fillWidth: true
    implicitHeight: layout.implicitHeight + 24
    radius: 18
    color: theme.cedarSoft
    Accessible.role: Accessible.StaticText
    Accessible.name: title + ". " + detail

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 12
        spacing: 12

        Glyph {
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            kind: banner.icon
            color: theme.cedar
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            Text {
                Layout.fillWidth: true
                text: banner.title
                color: theme.text
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.Bold
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: banner.detail
                color: theme.muted
                font.family: theme.ui
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
        }

        AbstractButton {
            visible: banner.secondaryText.length > 0
            implicitHeight: 36
            implicitWidth: secondaryLabel.implicitWidth + 16
            focusPolicy: Qt.NoFocus
            onClicked: banner.secondaryAction()
            Accessible.name: banner.secondaryText
            background: Rectangle { radius: 12; color: parent.down ? theme.pressed : "transparent" }
            contentItem: Text {
                id: secondaryLabel
                text: banner.secondaryText
                color: theme.muted
                font.family: theme.ui
                font.pixelSize: 13
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        AbstractButton {
            visible: banner.actionText.length > 0
            implicitHeight: 36
            implicitWidth: actionLabel.implicitWidth + 28
            focusPolicy: Qt.NoFocus
            onClicked: banner.action()
            Accessible.name: banner.actionText
            background: Rectangle {
                radius: 12
                color: theme.surface
                opacity: parent.down ? 0.8 : 1
            }
            contentItem: Text {
                id: actionLabel
                text: banner.actionText
                color: theme.cedar
                font.family: theme.ui
                font.pixelSize: 13
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
