// The first screen of a first run: what CedarView is, two promises about
// privacy, and two ways in — signing in, or looking around with sample data.
//
// CedarView's own screen, before anything of Microsoft's: the sign-in page
// only appears when "Sign in with Cedarville" is tapped, and then inside the
// app's chrome (SignInView).

import QtQuick
import QtQuick.Layouts

Item {
    id: root

    Theme { id: theme }

    // Navy, lit from the top-left, deepening downward.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: theme.navy }
            GradientStop { position: 0.68; color: theme.navyLo }
            GradientStop { position: 1.0; color: "#041A31" }
        }
    }
    Canvas {
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const reach = Math.max(width * 1.2, height * 0.7) * 0.62
            const light = ctx.createRadialGradient(width * 0.15, 0, 0, width * 0.15, 0, reach)
            light.addColorStop(0, "rgba(29, 96, 153, 1)")
            light.addColorStop(1, "rgba(29, 96, 153, 0)")
            ctx.fillStyle = light
            ctx.fillRect(0, 0, width, height)
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: Math.max(height, column.implicitHeight + column.y + 28)
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: column
            x: 24
            y: Math.max(40, root.SafeArea.margins.top + 32)
            width: parent.width - 48
            height: Math.max(implicitHeight, root.height - y - 28 - root.SafeArea.margins.bottom)
            spacing: 0

            Rectangle {
                Layout.preferredWidth: 76
                Layout.preferredHeight: 76
                radius: 22
                color: Qt.rgba(1, 1, 1, 0.1)
                Image {
                    anchors.centerIn: parent
                    width: 56
                    height: 56
                    source: "icon.png"
                    sourceSize: Qt.size(112, 112)
                    fillMode: Image.PreserveAspectFit
                    mipmap: true
                }
            }

            Text {
                Layout.topMargin: 22
                text: "CedarView"
                color: theme.navyText
                font.family: theme.display
                font.pixelSize: 46
                font.weight: Font.ExtraBold
                font.letterSpacing: -1.5
            }
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 12
                text: "Chapel skips, meals, flex, menus, hours and curfew. The parts of myCU you check every day, at a glance."
                color: theme.navyMuted
                font.family: theme.ui
                font.pixelSize: 17
                lineHeight: 1.2
                wrapMode: Text.Wrap
            }

            // A glimpse of the app: three panes of glass, gently tilted.
            Item {
                Layout.fillWidth: true
                Layout.topMargin: 26
                Layout.preferredHeight: 176
                Accessible.ignored: true

                Glass {
                    x: 0
                    y: 24
                    width: 136
                    height: 128
                    rotation: -6
                    Text { text: "SKIPS LEFT"; color: theme.navyMuted; font.family: theme.ui; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 1.1 }
                    Text { Layout.topMargin: 10; text: "16"; color: theme.goldHero; font.family: theme.display; font.pixelSize: 50; font.weight: Font.ExtraBold }
                    Text { Layout.topMargin: 2; text: "of 18"; color: theme.navyMuted; font.family: theme.ui; font.pixelSize: 12 }
                }
                Glass {
                    x: Math.min(112, parent.width / 2 - 75)
                    y: 0
                    z: 2
                    width: 150
                    height: 140
                    rotation: 2
                    fill: Qt.rgba(1, 1, 1, 0.1)
                    Text { text: "FLEX"; color: theme.navyMuted; font.family: theme.ui; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 1.1 }
                    Text { Layout.topMargin: 14; text: "$102.34"; color: theme.navyText; font.family: theme.display; font.pixelSize: 30; font.weight: Font.ExtraBold }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 16
                        implicitHeight: 8
                        radius: 4
                        color: Qt.rgba(1, 1, 1, 0.14)
                        Rectangle {
                            width: parent.width * 0.64
                            height: parent.height
                            radius: 4
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: theme.paceA }
                                GradientStop { position: 1.0; color: theme.paceB }
                            }
                        }
                    }
                    Text { Layout.topMargin: 8; text: "$8.43 a week"; color: theme.navyMuted; font.family: theme.ui; font.pixelSize: 12 }
                }
                Glass {
                    x: parent.width - width
                    y: 40
                    width: 124
                    height: 122
                    rotation: 7
                    Text { text: "LUNCH"; color: theme.navyMuted; font.family: theme.ui; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 1.1 }
                    Text { Layout.topMargin: 12; text: "10:30"; color: theme.navyText; font.family: theme.display; font.pixelSize: 32; font.weight: Font.ExtraBold }
                    Text { Layout.topMargin: 6; text: "Beef Ragu"; color: theme.navyMuted; font.family: theme.ui; font.pixelSize: 12 }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 30
                spacing: 14

                Promise {
                    icon: "shield"
                    lead: "Sign in on Microsoft's own page."
                    rest: "CedarView never sees your password."
                }
                Promise {
                    icon: "lock"
                    lead: "No servers, ads or analytics."
                    rest: "Your info stays on this phone."
                }
            }

            Item { Layout.fillHeight: true; Layout.minimumHeight: 24 }

            PrimaryButton {
                Layout.fillWidth: true
                text: "Sign in with Cedarville"
                onClicked: login.startSignIn()
            }
            PrimaryButton {
                Layout.fillWidth: true
                Layout.topMargin: 6
                quiet: true
                navy: true
                text: "Look around with sample data"
                onClicked: login.startPreview()
            }
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 4
                visible: login.status.length > 0
                horizontalAlignment: Text.AlignHCenter
                text: login.status
                color: theme.navyMuted
                font.family: theme.ui
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
        }
    }

    // A pane of frosted glass for the glimpse.
    component Glass: Rectangle {
        default property alias content: glassColumn.data
        property color fill: Qt.rgba(1, 1, 1, 0.075)
        radius: 24
        color: fill
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.12)
        antialiasing: true

        ColumnLayout {
            id: glassColumn
            x: 16
            y: 16
            width: parent.width - 32
            spacing: 0
        }
    }

    // One promise: an icon, the claim in bold, the rest of it.
    component Promise: RowLayout {
        property string icon
        property string lead
        property string rest
        Layout.fillWidth: true
        spacing: 12
        Glyph {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            kind: parent.icon
            color: theme.goldHero
        }
        Text {
            Layout.fillWidth: true
            text: "<b><font color=\"#F5F8FB\">" + parent.lead + "</font></b> " + parent.rest
            textFormat: Text.StyledText
            color: theme.navyMuted
            font.family: theme.ui
            font.pixelSize: 14
            lineHeight: 1.15
            wrapMode: Text.Wrap
        }
    }
}
