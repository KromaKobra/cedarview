import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Buildings tab: tonight's curfew, then every building's hours.
//
// Curfew comes from the `curfew` viewmodel (the rule is in core/curfew.h).
//
// The building hours are **hardcoded**, copied off Campus Security's page
// (sourceUrl) on 2026-09-29, the same way HoursView carries the dining hours.
// Times are minutes after midnight; a building that closes after midnight
// runs past 1440 (12:45am is 1485), which is how "open now" still holds at
// 12:30am on a Saturday under Friday's hours.
Item {
    id: root
    Theme { id: theme }

    readonly property string sourceUrl:
        "https://www.cedarville.edu/offices/campus-security/building-hours"

    // 0 Monday–Thursday, 1 Friday, 2 Saturday, 3 Sunday — the page's columns.
    function dayTypeOfWeekday(weekday) {
        return weekday === 0 ? 3 : weekday === 5 ? 1 : weekday === 6 ? 2 : 0
    }
    function minuteOf(date) { return date.getHours() * 60 + date.getMinutes() }

    property int weekday: new Date().getDay()
    property int nowMinute: minuteOf(new Date())
    readonly property int todayType: dayTypeOfWeekday(weekday)
    readonly property int yesterdayType: dayTypeOfWeekday((weekday + 6) % 7)
    property int dayType: todayType
    readonly property bool showingToday: dayType === todayType

    // Every second during the countdown, otherwise often enough for the
    // "open now" marks. Only while the tab is on screen.
    Timer {
        interval: curfew.countdownText.length > 0 ? 1000 : 15000
        repeat: true
        running: root.SwipeView.isCurrentItem && Qt.application.state === Qt.ApplicationActive
        triggeredOnStart: true
        onTriggered: {
            const now = new Date()
            root.weekday = now.getDay()
            root.nowMinute = root.minuteOf(now)
            curfew.refreshAll()
        }
    }

    // 450 -> "7:30am", 1485 -> "12:45am". The same shape as HoursView's.
    function clock(minute) {
        const h = Math.floor(minute / 60) % 24
        const m = minute % 60
        const hour12 = h % 12 === 0 ? 12 : h % 12
        return hour12 + (m > 0 ? ":" + (m < 10 ? "0" : "") + m : "") + (h < 12 ? "am" : "pm")
    }
    function span(range) { return clock(range[0]) + "–" + clock(range[1]) }

    // Open under today's hours, or still open under yesterday's past midnight.
    function isOpenNow(building) {
        const today = building.hours[root.todayType]
        const yesterday = building.hours[root.yesterdayType]
        return (today !== null && root.nowMinute >= today[0] && root.nowMinute < today[1])
               || (yesterday !== null && root.nowMinute + 1440 < yesterday[1])
    }

    // `hours` is per day type; null is closed. `code` is the page's building
    // abbreviation, where it gives one.
    //
    // Two cells on the page are not taken literally: BTS's Friday opening says
    // "6:30 p.m." between 6:30 a.m. on every other day, and is read as a typo;
    // Alford Auditorium's Saturday is only an asterisk pointing at its
    // authorized-access note, and is shown as closed with that note.
    readonly property var buildings: [
        { name: "Alford Annex", code: "AA",
          note: "Access card only after 5pm and on weekends",
          hours: [[360, 1380], [360, 1380], [420, 1380], [720, 1380]] },
        { name: "Alford Auditorium", code: "AL",
          note: "Authorized access only after 6pm and on weekends",
          hours: [[420, 1380], [420, 1380], null, null] },
        { name: "Apple Technology Resource Center", code: "APP",
          hours: [[420, 1425], [420, 1485], [420, 1485], [720, 1425]] },
        { name: "Callan Athletic Center", code: "CAL",
          hours: [[360, 1380], [360, 1320], [420, 1320], [840, 1080]] },
        { name: "Carnegie Center for the Visual Arts", code: "CNG",
          note: "Access card only after 6pm and on weekends",
          hours: [[420, 1380], [420, 1380], [420, 1380], [420, 1380]] },
        { name: "Centennial Library", code: "LB",
          hours: [[465, 1410], [465, 1140], [600, 1140], [930, 1410]] },
        { name: "Center for Biblical and Theological Studies", code: "BTS",
          hours: [[390, 1425], [390, 1485], [390, 1485], [720, 1425]] },
        { name: "Chemistry Lab Center",
          note: "Authorized access cards only after 6pm and on weekends",
          hours: [[420, 1080], [420, 1080], null, null] },
        { name: "Chick-fil-A",
          note: "The restaurant closes at 9pm except Sundays; the space stays open to students",
          hours: [[420, 1410], [420, 1410], [420, 1410], [720, 1410]] },
        { name: "Civil Engineering Center",
          hours: [[420, 1050], [420, 1050], null, null] },
        { name: "Dixon Ministry Center", code: "DMC",
          hours: [[420, 1380], [420, 1380], [420, 1380], [720, 1380]] },
        { name: "Engineering and Science Center", code: "ENS",
          note: "Engineering students use access cards after 6pm and on weekends",
          hours: [[360, 1425], [360, 1485], [420, 1485], [720, 1425]] },
        { name: "Engineering Projects Laboratory", code: "EPL",
          note: "Engineering students use access cards after 6pm and on weekends",
          hours: [[360, 1380], [360, 1380], [420, 1380], [720, 1380]] },
        { name: "Fitness Recreation Center", code: "FTR",
          hours: [[360, 1380], [360, 1320], [600, 1320], [840, 1200]] },
        { name: "Health Sciences Center", code: "HSC",
          note: "Pharmacy and nursing students use access cards after 6pm",
          hours: [[390, 1425], [390, 1485], [390, 1485], [390, 1425]] },
        { name: "Milner Hall",
          hours: [[420, 1425], [420, 1485], [720, 1485], [900, 1425]] },
        { name: "Scharnberg Business and Communication Center", code: "SBCC",
          hours: [[420, 1425], [420, 1485], [420, 1485], [720, 1425]] },
        { name: "Stevens Student Center", code: "SSC",
          hours: [[390, 1425], [390, 1485], [390, 1485], [390, 1425]] },
        { name: "Tyler Digital Communication Center", code: "TYL",
          hours: [[420, 1380], [420, 1380], [600, 1380], [720, 1380]] }
    ]

    component SectionLabel: Label {
        Layout.leftMargin: 2
        color: theme.faint
        font.pixelSize: 11
        font.bold: true
        font.letterSpacing: 1.4
        elide: Text.ElideRight
    }

    // One of the two rules in the curfew card's footer, lit when it is the one
    // in force tonight.
    component CurfewRule: ColumnLayout {
        property string days
        property string time
        property bool active: false
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        spacing: 1

        Label {
            Layout.fillWidth: true
            text: parent.days
            color: parent.active ? theme.heroAccent : Qt.rgba(0.96, 0.98, 1.0, 0.62)
            font.pixelSize: 10
            font.bold: true
            font.letterSpacing: 0.8
            elide: Text.ElideRight
        }
        Label {
            Layout.fillWidth: true
            text: parent.time
            color: parent.active ? theme.textOnDark : Qt.rgba(0.96, 0.98, 1.0, 0.62)
            font.pixelSize: 15
            font.bold: parent.active
            elide: Text.ElideRight
        }
    }

    // A building: name, its code and access note under it, the hours on the
    // right, and the gold "open now" dot in the gutter while showing today.
    component BuildingRow: RowLayout {
        id: row
        property string name
        property string detail
        property string hours
        property bool closed: false
        property bool open: false
        property bool first: false

        Layout.fillWidth: true
        Layout.topMargin: first ? 0 : 14
        spacing: 10

        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 5
            visible: root.showingToday
            width: 7
            height: 7
            radius: 3.5
            color: row.open ? theme.accent : "transparent"
        }

        ColumnLayout {
            Layout.fillWidth: true
            // Take what the hours leave, never what the text would like:
            // a wrapped label still reports its unwrapped width.
            Layout.preferredWidth: 0
            spacing: 2

            Label {
                Layout.fillWidth: true
                text: row.name
                color: row.closed ? theme.faint : theme.text
                font.pixelSize: 13
                font.bold: true
                wrapMode: Text.Wrap
            }

            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: row.detail
                color: theme.faint
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
        }

        Label {
            Layout.alignment: Qt.AlignTop
            text: row.hours
            color: row.closed ? theme.faint : theme.text
            font.pixelSize: 13
            font.bold: !row.closed
        }
    }

    Flickable {
        id: scroll
        anchors.fill: parent
        contentHeight: column.implicitHeight + theme.pageMargin * 2
        clip: true
        boundsBehavior: Flickable.DragOverBounds

        ColumnLayout {
            id: column
            x: theme.pageMargin
            y: theme.pageMargin
            width: scroll.width - theme.pageMargin * 2
            spacing: theme.gap

            // Curfew, in the same navy hero as the summary's next chapel.
            Card {
                padding: 0

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: curfewContent.implicitHeight + 40
                    radius: theme.cardRadius
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: theme.heroStart }
                        GradientStop { position: 1.0; color: theme.heroEnd }
                    }

                    ColumnLayout {
                        id: curfewContent
                        x: 20
                        y: 20
                        width: parent.width - 40
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                text: "CURFEW"
                                color: theme.heroAccent
                                font.pixelSize: 11
                                font.bold: true
                                font.letterSpacing: 1.4
                            }

                            Item { Layout.fillWidth: true }

                            Rectangle {
                                implicitWidth: nightBadge.implicitWidth + 20
                                implicitHeight: 25
                                radius: 13
                                color: Qt.rgba(1, 1, 1, 0.10)
                                border.width: 1
                                border.color: Qt.rgba(1, 1, 1, 0.12)

                                Label {
                                    id: nightBadge
                                    anchors.centerIn: parent
                                    text: curfew.nightText.toUpperCase()
                                    color: theme.textOnDark
                                    font.pixelSize: 10
                                    font.bold: true
                                    font.letterSpacing: 1.0
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.topMargin: 14
                            text: curfew.timeText
                            color: theme.textOnDark
                            font.pixelSize: 38
                            font.bold: true
                        }

                        // The last hour: the countdown takes over the footer.
                        // Otherwise the footer is the week's two rules.
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: 16
                            implicitHeight: 60
                            radius: 17
                            color: Qt.rgba(0, 0, 0, 0.16)

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                anchors.rightMargin: 16
                                visible: curfew.countdownText.length > 0
                                spacing: 10

                                Label {
                                    text: curfew.countdownText
                                    color: theme.heroAccent
                                    font.pixelSize: 30
                                    font.bold: true
                                    font.features: ({ "tnum": 1 })
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: "until curfew"
                                    color: Qt.rgba(0.96, 0.98, 1.0, 0.68)
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                anchors.rightMargin: 16
                                visible: curfew.countdownText.length === 0
                                spacing: 12

                                CurfewRule {
                                    days: "SUN–THU"
                                    time: "11:59 PM"
                                    active: !curfew.lateNight
                                }
                                CurfewRule {
                                    days: "FRI–SAT"
                                    time: "12:59 AM"
                                    active: curfew.lateNight
                                }
                            }
                        }
                    }
                }
            }

            SectionLabel {
                Layout.topMargin: 8
                text: "BUILDING HOURS"
            }

            SegmentedControl {
                Layout.fillWidth: true
                implicitHeight: 36
                fontSize: 12
                labels: ["Mon–Thu", "Fri", "Sat", "Sun"]
                currentIndex: root.dayType
                onActivated: (index) => root.dayType = index
            }

            Card {
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.bottomMargin: 12
                        spacing: 6

                        Label {
                            Layout.fillWidth: true
                            text: "ALL BUILDINGS"
                            color: theme.cedar
                            font.pixelSize: 10
                            font.bold: true
                            font.letterSpacing: 1.1
                            elide: Text.ElideRight
                        }
                        Rectangle {
                            visible: root.showingToday
                            width: 7
                            height: 7
                            radius: 3.5
                            color: theme.accent
                        }
                        Label {
                            visible: root.showingToday
                            text: "Open now"
                            color: theme.muted
                            font.pixelSize: 10
                        }
                    }

                    Repeater {
                        model: root.buildings

                        BuildingRow {
                            required property var modelData
                            required property int index
                            readonly property var today: modelData.hours[root.dayType]
                            first: index === 0
                            name: modelData.name
                            detail: [modelData.code, modelData.note].filter(Boolean).join(" · ")
                            closed: today === null
                            hours: today === null ? "Closed" : root.span(today)
                            open: root.showingToday && root.isOpenNow(modelData)
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 2
                Layout.rightMargin: 2
                Layout.topMargin: 2
                text: "Academic buildings lock at 6pm and are key card only after that unless noted. "
                      + "Hours over the summer and holidays can differ."
                color: theme.faint
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }

            Item { Layout.preferredHeight: 8 }
        }
    }
}
