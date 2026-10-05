// Dining › Hours: what is open now and what opens next, the day as a
// timeline, and the meal-swipe periods.
//
// The hours are hand-entered in src/core/hours.cpp (copied off The Commons'
// dining information page); the `hours` viewmodel asks them about the clock.
// Nothing here is a table of its own.

import QtQuick
import QtQuick.Layouts

ScrollPage {
    id: root

    Theme { id: theme }

    HeroCard {
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            LiveDot {
                visible: hours.anyOpen
            }
            Text {
                Layout.fillWidth: true
                text: hours.anyOpen ? "OPEN NOW" : "CLOSED NOW"
                color: theme.goldHero
                font.family: theme.ui
                font.pixelSize: 11
                font.weight: Font.Bold
                font.letterSpacing: 1.1
            }
            Text {
                text: hours.todayTypeText
                color: theme.navyMuted
                font.family: theme.ui
                font.pixelSize: 12
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 10
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: hours.anyOpen ? hours.openName : "Nothing open right now"
                color: theme.navyText
                font.family: theme.display
                font.pixelSize: hours.anyOpen ? 30 : 24
                font.weight: Font.Bold
                wrapMode: Text.Wrap
            }
            Text {
                visible: hours.anyOpen
                text: hours.openUntil
                color: theme.navyMuted
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
        }

        Flow {
            visible: hours.alsoOpen.length > 0
            Layout.fillWidth: true
            Layout.topMargin: 10
            spacing: 6
            Repeater {
                model: hours.alsoOpen
                Chip {
                    required property string modelData
                    text: modelData
                    textColor: theme.navyText
                    fill: theme.heroChip
                }
            }
        }

        Rectangle {
            visible: hours.nextOpeningText.length > 0
            Layout.fillWidth: true
            Layout.topMargin: 14
            Layout.bottomMargin: 12
            implicitHeight: 1
            color: theme.heroLine
        }
        Text {
            visible: hours.nextOpeningText.length > 0
            Layout.fillWidth: true
            text: hours.nextOpeningText + "  <font color=\"#C2F5F8FB\">· " + hours.nextOpeningIn + "</font>"
            textFormat: Text.StyledText
            color: theme.navyText
            font.family: theme.ui
            font.pixelSize: 14
            font.weight: Font.Bold
        }
        Flow {
            visible: hours.nextOpeningNames.length > 0
            Layout.fillWidth: true
            Layout.topMargin: 10
            spacing: 6
            Repeater {
                model: hours.nextOpeningNames
                Chip {
                    required property string modelData
                    text: modelData
                    textColor: theme.navyText
                    fill: theme.heroChip
                }
            }
        }
    }

    // ---- The day, as a timeline --------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 8
        Layout.leftMargin: 4
        spacing: 8

        Text {
            Layout.fillWidth: true
            text: (hours.showingToday ? "Today at a glance" : "Hours").toUpperCase()
            color: theme.faint
            font.family: theme.ui
            font.pixelSize: 11
            font.weight: Font.Bold
            font.letterSpacing: 1.1
        }
        SegmentedControl {
            implicitWidth: 196
            implicitHeight: 38
            radius: 12
            fontSize: 12
            labels: hours.dayTypeLabels
            currentIndex: hours.dayType
            onActivated: (index) => hours.setDayType(index)
        }
    }

    Card {
        topPadding: 14
        bottomPadding: 12

        // The axis, with now on it.
        Item {
            Layout.fillWidth: true
            implicitHeight: 22

            Repeater {
                model: hours.ticks
                Text {
                    required property var modelData
                    required property int index
                    x: parent.width * modelData.left - (index === 0 ? 0 : width / 2)
                    y: 4
                    text: modelData.label
                    color: theme.faint
                    font.family: theme.ui
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }
            Rectangle {
                visible: hours.nowFraction >= 0
                x: Math.min(parent.width - width, Math.max(0, parent.width * hours.nowFraction - width / 2))
                width: nowTag.implicitWidth + 12
                height: 20
                radius: 6
                color: theme.gold
                Text {
                    id: nowTag
                    anchors.centerIn: parent
                    text: hours.nowText
                    color: theme.bg
                    font.family: theme.display
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }
            }
        }

        Repeater {
            model: hours.timeline

            ColumnLayout {
                id: place
                required property string name
                required property string status
                required property bool live
                required property var segments
                Layout.fillWidth: true
                Layout.topMargin: 9
                Layout.bottomMargin: 0
                spacing: 7

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: place.name
                        color: theme.text
                        font.family: theme.ui
                        font.pixelSize: 14
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                    }
                    Text {
                        text: place.status
                        color: place.live ? theme.gold : theme.muted
                        font.family: theme.ui
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                }

                Item {
                    Layout.fillWidth: true
                    implicitHeight: 12

                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: theme.surface2
                    }
                    Repeater {
                        model: place.segments
                        Rectangle {
                            required property var modelData
                            x: parent.width * modelData.left + 1
                            width: Math.max(2, parent.width * modelData.width - 2)
                            height: parent.height
                            radius: 6
                            opacity: modelData.kind === "past" || modelData.kind === "flexOnly" ? 0.4 : 1
                            color: modelData.kind === "past" ? theme.faint : theme.meterB
                            gradient: modelData.kind === "now" ? gold
                                    : modelData.kind === "past" ? null : blue
                            Gradient {
                                id: gold
                                GradientStop { position: 0.0; color: theme.goldA }
                                GradientStop { position: 1.0; color: theme.goldB }
                            }
                            Gradient {
                                id: blue
                                GradientStop { position: 0.0; color: theme.meterA }
                                GradientStop { position: 1.0; color: theme.meterB }
                            }
                        }
                    }
                    Rectangle {
                        visible: hours.nowFraction >= 0
                        x: parent.width * hours.nowFraction - 1
                        y: -3
                        width: 2
                        height: parent.height + 6
                        radius: 1
                        color: theme.gold
                    }
                }
            }
        }

        // The key.
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 14
            implicitHeight: 1
            color: theme.line
        }
        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 12
            spacing: 14

            Repeater {
                model: [
                    { label: "Open now", kind: "now" },
                    { label: "Later today", kind: "next" },
                    { label: "Flex or card only", kind: "flexOnly" },
                    { label: "Earlier", kind: "past" }
                ]
                Row {
                    required property var modelData
                    spacing: 6
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 14
                        height: 8
                        radius: 4
                        color: parent.modelData.kind === "past" ? theme.faint
                             : parent.modelData.kind === "now" ? theme.gold : theme.meterB
                        opacity: parent.modelData.kind === "past" || parent.modelData.kind === "flexOnly" ? 0.4 : 1
                    }
                    Text {
                        text: parent.modelData.label
                        color: theme.muted
                        font.family: theme.ui
                        font.pixelSize: 12
                    }
                }
            }
        }
    }

    // ---- Swipe periods ---------------------------------------------------------
    Card {
        topPadding: 4
        bottomPadding: 4

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 10
            Layout.bottomMargin: 6
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: "Meal swipe periods"
                color: theme.text
                font.family: theme.ui
                font.pixelSize: 15
                font.weight: Font.Bold
            }
            Text {
                text: dining.scansPerPeriod === 5 ? "Up to five swipes each" : "One swipe each"
                color: theme.faint
                font.family: theme.ui
                font.pixelSize: 12
            }
        }

        Repeater {
            model: hours.swipePeriods

            ColumnLayout {
                id: period
                required property var modelData
                Layout.fillWidth: true
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: theme.line
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 11
                    Layout.bottomMargin: 11
                    spacing: 10
                    Text {
                        Layout.fillWidth: true
                        text: period.modelData.name
                        color: theme.text
                        font.family: theme.ui
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    Chip {
                        visible: period.modelData.current
                        text: "Now"
                        implicitHeight: 22
                        fontSize: 11
                        textColor: theme.gold
                        fill: theme.goldSoft
                    }
                    Text {
                        Layout.preferredWidth: 140
                        horizontalAlignment: Text.AlignRight
                        text: period.modelData.text
                        color: theme.text
                        font.family: theme.display
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        text: "From The Commons dining information page. Hours change over breaks."
        color: theme.faint
        font.family: theme.ui
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }
}
