// Tonight's curfew, as a hero: when it falls and, in the evening, how long is
// left, with a bar from 8 PM to curfew. Leads Today at night and the Campus
// tab always. `showRules` adds the week's two rules, the one in force lit.

import QtQuick
import QtQuick.Layouts

HeroCard {
    id: hero

    property bool showRules: true

    Theme { id: theme }

    readonly property bool evening: curfew.eveningFraction >= 0

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Glyph {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            kind: "moon"
            color: theme.goldHero
        }
        Text {
            Layout.fillWidth: true
            text: "CURFEW TONIGHT"
            color: theme.goldHero
            font.family: theme.ui
            font.pixelSize: 11
            font.weight: Font.Bold
            font.letterSpacing: 1.1
        }
        ClockText {
            visible: hero.evening
            text: curfew.timeText
            color: theme.navyText
            meridiemColor: theme.navyMuted
            size: 16
        }
    }

    // In the evening the time left leads; before that, the time itself.
    Text {
        Layout.fillWidth: true
        Layout.topMargin: 12
        text: hero.evening ? curfew.timeLeftText : curfew.timeText
        color: theme.goldHero
        font.family: theme.display
        font.pixelSize: hero.evening ? 66 : 54
        font.weight: Font.ExtraBold
        font.letterSpacing: -2
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 40
    }
    Text {
        Layout.fillWidth: true
        Layout.topMargin: 6
        text: curfew.nightText + " · " + curfew.ruleText
        color: theme.navyMuted
        font.family: theme.ui
        font.pixelSize: 13
        elide: Text.ElideRight
    }

    // 8 PM to curfew, filled to now.
    Item {
        visible: hero.evening
        Layout.fillWidth: true
        Layout.topMargin: 16
        implicitHeight: 10

        Rectangle {
            anchors.fill: parent
            radius: 5
            color: Qt.rgba(0, 0.04, 0.1, 0.35)
        }
        Rectangle {
            width: Math.max(10, parent.width * curfew.eveningFraction)
            height: parent.height
            radius: 5
            gradient: Gradient {
                GradientStop { position: 0.0; color: theme.heroPipA }
                GradientStop { position: 1.0; color: theme.heroPipB }
            }
        }
    }
    Item {
        visible: hero.evening
        Layout.fillWidth: true
        Layout.topMargin: 7
        implicitHeight: 16

        Text {
            text: "8 PM"
            color: theme.navyMuted
            font.family: theme.ui
            font.pixelSize: 12
        }
        Text {
            id: nowLabel
            x: Math.max(40, Math.min(parent.width - width - 56, parent.width * curfew.eveningFraction - width / 2))
            text: "Now " + curfew.nowText
            color: theme.navyText
            font.family: theme.ui
            font.pixelSize: 12
            font.weight: Font.Bold
        }
        Text {
            anchors.right: parent.right
            text: curfew.timeText
            color: theme.navyMuted
            font.family: theme.ui
            font.pixelSize: 12
        }
    }

    GridLayout {
        visible: hero.showRules
        Layout.fillWidth: true
        Layout.topMargin: 16
        columns: 2
        columnSpacing: 8

        Repeater {
            model: [
                { days: "Sunday – Thursday", time: "11:59 PM", active: !curfew.lateNight },
                { days: "Friday & Saturday", time: "12:59 AM", active: curfew.lateNight }
            ]

            Rectangle {
                id: ruleBox
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: rule.implicitHeight + 20
                radius: 16
                color: modelData.active ? Qt.rgba(0.99, 0.72, 0.07, 0.12) : theme.heroInset
                border.width: modelData.active ? 1 : 0
                border.color: Qt.rgba(0.99, 0.72, 0.07, 0.45)

                ColumnLayout {
                    id: rule
                    x: 12
                    y: 10
                    width: parent.width - 24
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: ruleBox.modelData.days
                        color: theme.navyMuted
                        font.family: theme.ui
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Text {
                        text: ruleBox.modelData.time
                        color: ruleBox.modelData.active ? theme.navyText : theme.navyMuted
                        font.family: theme.display
                        font.pixelSize: 18
                        font.weight: Font.Bold
                    }
                }
            }
        }
    }
}
