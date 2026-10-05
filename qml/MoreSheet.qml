// The More menu, as a sheet: appearance, about, the privacy policy, and
// signing in or out — whichever applies.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

BottomSheet {
    id: sheet

    Theme { id: theme }

    readonly property string privacyPolicyUrl: "https://github.com/KromaKobra/cedarview/blob/main/PRIVACY.md"
    property bool showAbout: false

    onClosed: showAbout = false

    Text {
        Layout.fillWidth: true
        text: "CedarView"
        color: theme.text
        font.family: theme.display
        font.pixelSize: 24
        font.weight: Font.Bold
    }
    Text {
        Layout.fillWidth: true
        Layout.topMargin: 2
        Layout.bottomMargin: 10
        text: "Version " + bridge.version
        color: theme.faint
        font.family: theme.ui
        font.pixelSize: 13
    }

    MenuLine {
        icon: "sun"
        label: "Light theme"
        detail: "A brighter palette for daylight"
        // The whole line is the control; the switch only shows the state, so
        // one tap is never counted twice.
        ToggleSwitch {
            Layout.alignment: Qt.AlignVCenter
            enabled: false
            on: settings.lightMode
        }
        onTapped: settings.toggleLightMode()
    }
    MenuLine {
        icon: "info"
        label: "About CedarView"
        onTapped: sheet.showAbout = !sheet.showAbout
    }
    Text {
        visible: sheet.showAbout
        Layout.fillWidth: true
        Layout.leftMargin: 46
        Layout.bottomMargin: 10
        text: "The parts of myCU you check every day, at a glance. Your password is never seen or "
              + "stored by CedarView: sign-in happens on Microsoft's own page. The app keeps a copy "
              + "of what it last loaded on this phone, so it opens on your figures, and deletes it "
              + "when you sign out. No servers, no ads, no analytics.\n\nBackend: " + bridge.platformName
        color: theme.muted
        font.family: theme.ui
        font.pixelSize: 13
        lineHeight: 1.15
        wrapMode: Text.Wrap
    }
    MenuLine {
        icon: "shield"
        label: "Privacy policy"
        trailing: "external"
        onTapped: Qt.openUrlExternally(sheet.privacyPolicyUrl)
    }
    MenuLine {
        visible: login.inPreview
        icon: "lock"
        label: "Sign in with Cedarville"
        detail: "Leave the sample data"
        onTapped: {
            sheet.close()
            login.startSignIn()
        }
    }
    MenuLine {
        visible: !login.inPreview
        icon: "signOut"
        label: "Sign out"
        detail: "Deletes your saved records from this phone"
        danger: true
        onTapped: {
            sheet.close()
            login.signOut()
        }
    }

    // One line of the menu: an icon, what it does, and whatever goes on the
    // right (the theme switch).
    component MenuLine: Item {
        id: line
        default property alias trailingItems: trailingArea.data
        property string icon
        property string label
        property string detail
        property string trailing
        property bool danger: false
        signal tapped()

        Layout.fillWidth: true
        implicitHeight: Math.max(56, lineLayout.implicitHeight + 20)

        TapHandler {
            id: lineTap
            onTapped: line.tapped()
        }
        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: -10
            anchors.rightMargin: -10
            radius: 14
            color: lineTap.pressed ? theme.pressed : "transparent"
        }

        RowLayout {
            id: lineLayout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 14

            Rectangle {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                radius: 11
                color: line.danger ? theme.dangerSoft : theme.surface2
                Glyph {
                    anchors.centerIn: parent
                    width: 17
                    height: 17
                    kind: line.icon
                    color: line.danger ? theme.danger : theme.text
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    Layout.fillWidth: true
                    text: line.label
                    color: line.danger ? theme.danger : theme.text
                    font.family: theme.ui
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: line.detail
                    color: theme.faint
                    font.family: theme.ui
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
            }
            RowLayout {
                id: trailingArea
            }
            Glyph {
                visible: line.trailing.length > 0
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                kind: line.trailing
                color: theme.faint
            }
        }
    }
}
