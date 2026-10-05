// What stands where a source's figures would be, when there are none and
// none are coming: "Sign in to see your skips", or why the fetch failed, with
// the one thing to do about it. Hidden while the source is loading (that is
// the skeleton's moment) and whenever it has data — a failed refresh never
// blanks figures already shown.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: state

    // The source's SourceStatus.
    property var status: null
    // What would be here: "your skips", "the menu".
    property string what
    property bool navy: false

    Theme { id: theme }

    readonly property bool signIn: status !== null && status.needsSignIn
    readonly property bool failed: status !== null && !status.loading && status.error.length > 0

    visible: status !== null && !status.hasData && !status.loading && (signIn || failed)
    spacing: 8

    Text {
        Layout.fillWidth: true
        text: state.signIn ? "Sign in to see " + state.what + "."
                           : "Couldn't load " + state.what + ". " + (state.status ? state.status.error : "")
        color: state.navy ? theme.navyMuted : theme.muted
        font.family: theme.ui
        font.pixelSize: 13
        wrapMode: Text.Wrap
        maximumLineCount: 3
        elide: Text.ElideRight
    }

    AbstractButton {
        id: action
        implicitHeight: 34
        implicitWidth: actionLabel.implicitWidth + 28
        focusPolicy: Qt.NoFocus
        text: state.signIn ? "Sign in" : "Retry"
        onClicked: state.signIn ? login.startSignIn() : sync.refreshAll()
        Accessible.name: text
        background: Rectangle {
            radius: 12
            color: state.navy ? theme.heroChip : theme.cedarSoft
            opacity: action.down ? 0.75 : 1
        }
        contentItem: Text {
            id: actionLabel
            text: action.text
            color: state.navy ? theme.navyText : theme.cedar
            font.family: theme.ui
            font.pixelSize: 13
            font.weight: Font.Bold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
