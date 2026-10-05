// The Chapel tab: skips left, the upcoming schedule by week, the skip ledger,
// and why attendance is required.
//
// Two unrelated sources — the skip ledger behind the sign-in, and the public
// schedule — each with its own skeleton, stamp and error, so one never
// blanks the other.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollPage {
    id: root

    signal showChapel(var details)

    Theme { id: theme }

    readonly property var skips: chapel.skipsStatus
    readonly property var schedule: chapel.scheduleStatus
    // Weeks shown before "Show more"; the rest on request.
    property int weeksShown: 2

    // ---- Skips left ------------------------------------------------------------
    HeroCard {
        padding: 20

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: "SKIPS LEFT"
                color: theme.goldHero
                font.family: theme.ui
                font.pixelSize: 11
                font.weight: Font.Bold
                font.letterSpacing: 1.1
            }
            StatusStamp {
                status: root.skips
                navy: true
                verbose: true
            }
            Chip {
                visible: root.skips.hasData && !root.skips.showStamp
                text: chapel.inGoodStanding ? "Good standing" : "Needs attention"
                glyph: chapel.inGoodStanding ? "checkCircle" : "info"
                textColor: chapel.inGoodStanding ? theme.heroSky : theme.heroDanger
                fill: chapel.inGoodStanding ? Qt.rgba(0.663, 0.824, 0.961, 0.14) : Qt.rgba(1, 0.655, 0.624, 0.14)
            }
        }

        Skeleton {
            visible: !root.skips.hasData && root.skips.loading
            navy: true
            Layout.topMargin: 14
            Layout.preferredWidth: 150
            Layout.preferredHeight: 70
            radius: 14
        }
        Row {
            visible: root.skips.hasData
            Layout.topMargin: 6
            Text {
                id: bigFigure
                text: chapel.remaining >= 0 ? chapel.remaining : "—"
                color: theme.goldHero
                font.family: theme.display
                font.pixelSize: 92
                font.weight: Font.ExtraBold
                font.letterSpacing: -4
            }
            Text {
                anchors.baseline: bigFigure.baseline
                visible: chapel.allowed >= 0
                text: " / " + chapel.allowed
                color: theme.navyMuted
                font.family: theme.display
                font.pixelSize: 30
                font.weight: Font.DemiBold
            }
        }

        PipMeter {
            visible: root.skips.hasData && chapel.allowed > 0 && chapel.remaining >= 0
            Layout.fillWidth: true
            Layout.topMargin: 10
            total: chapel.allowed
            filled: chapel.remaining
            pipHeight: 12
            gap: 4
            colorA: theme.heroPipA
            colorB: theme.heroPipB
            offColor: Qt.rgba(1, 1, 1, 0.15)
        }
        RowLayout {
            visible: root.skips.hasData
            Layout.fillWidth: true
            Layout.topMargin: 9
            Text {
                text: chapel.used >= 0 ? chapel.used + " used" : ""
                color: theme.navyMuted
                font.family: theme.ui
                font.pixelSize: 13
            }
            Item { Layout.fillWidth: true }
            Text {
                text: chapel.skipsPerWeekText
                color: theme.navyText
                font.family: theme.ui
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.topMargin: 12
            status: root.skips
            what: "your skips"
            navy: true
        }

        Rectangle {
            visible: root.skips.hasData && chapel.allowance.length > 0
            Layout.fillWidth: true
            Layout.topMargin: 16
            Layout.bottomMargin: 14
            implicitHeight: 1
            color: theme.heroLine
        }
        GridLayout {
            visible: root.skips.hasData && chapel.allowance.length > 0
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12

            Repeater {
                model: chapel.allowance

                ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 2
                    Text {
                        text: parent.modelData.figure
                        color: theme.navyText
                        font.family: theme.display
                        font.pixelSize: 21
                        font.weight: Font.Bold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: parent.modelData.label
                        color: theme.navyMuted
                        font.family: theme.ui
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // ---- Upcoming ----------------------------------------------------------------
    SectionCaption {
        text: "Upcoming"
        note: root.schedule.showStamp ? root.schedule.updatedText : "Weekdays at 10:00 AM"
    }

    Card {
        topPadding: 4
        bottomPadding: 12

        Repeater {
            id: scheduleRows
            model: chapel.schedule

            Item {
                id: entry
                required property int index
                required property bool isHeader
                required property string heading
                required property string who
                required property string subtitle
                required property string description
                required property string dayName
                required property string dayNumber
                required property string timeText
                required property string badge
                required property bool isToday
                required property bool livestream
                required property string part
                required property string youtubeId
                required property var startsAt
                required property string dateText
                required property int weekIndex
                required property bool firstInWeek

                Layout.fillWidth: true
                visible: weekIndex < root.weeksShown
                implicitHeight: visible ? (isHeader ? headerText.implicitHeight + 18 : chapelRow.implicitHeight + 24) : 0

                // A rule between two chapels, not under a week's heading.
                Rectangle {
                    visible: !entry.isHeader && !entry.firstInWeek
                    width: parent.width
                    height: 1
                    color: theme.line
                }

                Text {
                    id: headerText
                    visible: entry.isHeader
                    y: 14
                    text: entry.heading.toUpperCase()
                    color: theme.cedar
                    font.family: theme.ui
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    font.letterSpacing: 1.1
                }

                TapHandler {
                    id: rowTap
                    enabled: !entry.isHeader
                    onTapped: root.showChapel({
                        who: entry.who, subtitle: entry.subtitle, description: entry.description,
                        dateText: entry.dateText, timeText: entry.timeText, startsAt: entry.startsAt,
                        livestream: entry.livestream, youtubeId: entry.youtubeId, isToday: entry.isToday
                    })
                }
                Rectangle {
                    visible: !entry.isHeader
                    anchors.fill: parent
                    anchors.leftMargin: -8
                    anchors.rightMargin: -8
                    radius: 12
                    color: rowTap.pressed ? theme.pressed : "transparent"
                }

                RowLayout {
                    id: chapelRow
                    visible: !entry.isHeader
                    anchors.left: parent.left
                    anchors.right: parent.right
                    y: 12
                    spacing: 12

                    Rectangle {
                        Layout.preferredWidth: 46
                        Layout.preferredHeight: 54
                        Layout.alignment: Qt.AlignTop
                        radius: 16
                        color: entry.isToday ? theme.goldSoft : theme.surface2
                        border.width: entry.isToday ? 1.5 : 0
                        border.color: theme.gold

                        Column {
                            anchors.centerIn: parent
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: entry.dayName
                                color: entry.isToday ? theme.gold : theme.faint
                                font.family: theme.ui
                                font.pixelSize: 10
                                font.weight: Font.Bold
                                font.letterSpacing: 0.8
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: entry.dayNumber
                                color: theme.text
                                font.family: theme.display
                                font.pixelSize: 20
                                font.weight: Font.Bold
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        spacing: 3

                        Flow {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: entry.who
                                color: theme.text
                                font.family: theme.ui
                                font.pixelSize: 15
                                font.weight: Font.Bold
                            }
                            Chip {
                                visible: entry.badge.length > 0
                                text: entry.badge
                                implicitHeight: 22
                                fontSize: 11
                                textColor: theme.gold
                                fill: theme.goldSoft
                            }
                            Chip {
                                visible: entry.part.length > 0
                                text: entry.part
                                implicitHeight: 22
                                fontSize: 11
                                textColor: theme.cedar
                                fill: theme.cedarSoft
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: entry.subtitle.length > 0 ? entry.subtitle : entry.description
                            color: theme.muted
                            font.family: theme.ui
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                        RowLayout {
                            visible: entry.livestream && entry.isToday
                            Layout.topMargin: 3
                            spacing: 5
                            Glyph {
                                Layout.preferredWidth: 15
                                Layout.preferredHeight: 15
                                kind: "live"
                                color: theme.cedar
                            }
                            Text {
                                text: "Livestreamed"
                                color: theme.cedar
                                font.family: theme.ui
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }
            }
        }

        // While the schedule loads with nothing saved: three rows' worth.
        Repeater {
            model: scheduleRows.count === 0 && root.schedule.loading ? 3 : 0

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 14
                spacing: 12
                Skeleton { Layout.preferredWidth: 46; Layout.preferredHeight: 54; radius: 16 }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Skeleton { Layout.preferredWidth: 150; Layout.preferredHeight: 14 }
                    Skeleton { Layout.preferredWidth: 210; Layout.preferredHeight: 11 }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 14
            visible: scheduleRows.count === 0 && !root.schedule.loading
            text: chapel.scheduleEmptyText
            color: theme.muted
            font.family: theme.ui
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }

        AbstractButton {
            id: moreWeeks
            readonly property int weeks: chapel.scheduleWeeks
            visible: weeks > root.weeksShown
            Layout.fillWidth: true
            Layout.topMargin: 8
            implicitHeight: 46
            focusPolicy: Qt.NoFocus
            text: "Show " + (weeks - root.weeksShown) + " more " + (weeks - root.weeksShown === 1 ? "week" : "weeks")
            onClicked: root.weeksShown = weeks
            Accessible.name: text
            background: Rectangle {
                radius: 14
                color: moreWeeks.down ? theme.pressed : theme.surface2
            }
            contentItem: Row {
                spacing: 6
                anchors.centerIn: parent
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: moreWeeks.text
                    color: theme.cedar
                    font.family: theme.ui
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Glyph {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    kind: "chevronDown"
                    color: theme.cedar
                }
            }
        }
    }

    // ---- The ledger ----------------------------------------------------------------
    SectionCaption {
        visible: root.skips.hasData
        text: "Skip history"
        note: chapel.historyText
    }

    Card {
        visible: root.skips.hasData
        topPadding: 4
        bottomPadding: 4

        Repeater {
            id: ledgerRows
            model: chapel.records

            Item {
                id: ledgerEntry
                required property int index
                required property int count
                required property string title
                required property string detail
                Layout.fillWidth: true
                implicitHeight: ledgerRow.implicitHeight + 26

                Rectangle {
                    visible: ledgerEntry.index > 0
                    width: parent.width
                    height: 1
                    color: theme.line
                }

                RowLayout {
                    id: ledgerRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12

                    // A skip, gold; a skip given back, blue.
                    Rectangle {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        radius: 14
                        color: ledgerEntry.count > 0 ? theme.goldSoft : theme.cedarSoft
                        Text {
                            anchors.centerIn: parent
                            text: (ledgerEntry.count > 0 ? "+" : "−") + Math.abs(ledgerEntry.count)
                            color: ledgerEntry.count > 0 ? theme.gold : theme.cedar
                            font.family: theme.display
                            font.pixelSize: 15
                            font.weight: Font.Bold
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: ledgerEntry.title
                            color: theme.text
                            font.family: theme.ui
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: ledgerEntry.detail
                            color: theme.muted
                            font.family: theme.ui
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 10
            Layout.bottomMargin: 10
            visible: ledgerRows.count === 0
            text: "Nothing recorded against your skips this term."
            color: theme.muted
            font.family: theme.ui
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
    }

    // ---- Why it's required ---------------------------------------------------------
    Card {
        visible: root.skips.hasData && chapel.requirementReasons.length > 0

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Glyph {
                Layout.preferredWidth: 17
                Layout.preferredHeight: 17
                kind: "info"
                color: theme.muted
            }
            Text {
                Layout.fillWidth: true
                text: "Why you're required to attend"
                color: theme.text
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.Bold
            }
        }
        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 11
            spacing: 6

            Repeater {
                model: chapel.requirementReasons
                Chip {
                    required property string modelData
                    text: modelData
                }
            }
        }
    }
}
