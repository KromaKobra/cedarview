// A tab that exists in the bar but not yet in the app.
//
// Shown rather than hidden on purpose. The tab bar is the app's map, and a map
// that grows new roads as they are built tells you less than one that shows
// where the roads are going — "Coming soon" under a named tab answers "is this
// app going to do that?", which a missing tab does not.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property string title
    property string note

    Theme { id: theme }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 72, 320)
        spacing: 10

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: root.title
            color: theme.muted
            font.pixelSize: 13
            font.bold: true
            font.letterSpacing: 1.2
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "Coming soon!"
            color: theme.accent
            font.pixelSize: 26
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 4
            visible: text.length > 0
            text: root.note
            color: theme.faint
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
    }
}
