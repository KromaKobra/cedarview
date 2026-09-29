import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Dining hours and meal periods, one day type at a time.
//
// **Hardcoded**, copied off the dining information page (sourceUrl) on
// 2026-09-28; nothing publishes these as data. Chuck's sittings also live in
// servingHours() in src/core/providers/dining.cpp, which drives the summary
// card, so a change to The Commons' hours belongs in both places.
//
// Times are minutes after midnight, and every label is made from them, so the
// "open now" marks can never disagree with the hours shown.
Item {
    id: root
    Theme { id: theme }

    readonly property string sourceUrl:
        "https://www.cedarville.edu/offices/the-commons/dining-information"

    // 0 Monday–Friday, 1 Saturday, 2 Sunday — the three columns the page uses.
    function dayTypeOf(date) {
        const weekday = date.getDay()
        return weekday === 0 ? 2 : weekday === 6 ? 1 : 0
    }

    property int todayType: dayTypeOf(new Date())
    property int nowMinute: minuteOf(new Date())
    property int dayType: todayType
    readonly property bool showingToday: dayType === todayType

    function minuteOf(date) { return date.getHours() * 60 + date.getMinutes() }

    Timer {
        interval: 30000
        repeat: true
        running: root.visible
        triggeredOnStart: true
        onTriggered: {
            const now = new Date()
            root.todayType = root.dayTypeOf(now)
            root.nowMinute = root.minuteOf(now)
        }
    }

    // 450 -> "7:30am", 720 -> "12pm". The same shape as the menu's hours.
    function clock(minute) {
        const h = Math.floor(minute / 60) % 24
        const m = minute % 60
        const hour12 = h % 12 === 0 ? 12 : h % 12
        return hour12 + (m > 0 ? ":" + (m < 10 ? "0" : "") + m : "") + (h < 12 ? "am" : "pm")
    }
    function span(range) { return clock(range[0]) + "–" + clock(range[1]) }
    function isNow(range) {
        return root.showingToday && range !== null
               && root.nowMinute >= range[0] && root.nowMinute < range[1]
    }

    // Per day type; a missing sitting is left out, not shown as "N/A".
    readonly property var commons: [
        [{ name: "Hot breakfast", hours: [420, 495] },
         { name: "Continental breakfast", hours: [495, 570] },
         { name: "Lunch", hours: [630, 870] },
         { name: "Dinner", hours: [990, 1170] }],
        [{ name: "Continental breakfast", hours: [480, 540] },
         { name: "Brunch", hours: [660, 780] },
         { name: "Dinner", hours: [990, 1110] }],
        [{ name: "Hot breakfast", hours: [480, 540] },
         { name: "Lunch", hours: [690, 840] },
         { name: "Dinner", hours: [1020, 1170] }]
    ]

    // `hours` and `exchange` are per day type; null is closed / none.
    readonly property var venues: [
        { name: "The Commons Market", note: "Grab+Go meals at lunch and dinner",
          hours: [[630, 1380], [660, 1320], [690, 1380]] },
        { name: "The Commons Express", note: "Pizza and tacos",
          hours: [[1200, 1380], null, [1200, 1380]] },
        { name: "Rinnova",
          hours: [[450, 1140], [630, 960], null] },
        { name: "Chick-fil-A / Tossed",
          hours: [[630, 1260], [630, 1260], null],
          exchange: [[630, 1200], [630, 1200], null] },
        { name: "Panda Express",
          hours: [[630, 1260], [630, 1260], null],
          exchange: [[630, 1200], [630, 1200], null] },
        { name: "The Cafe",
          hours: [[630, 1260], null, [840, 1260]],
          exchange: [[630, 1200], null, [840, 1200]] },
        { name: "Grab+Go Market (BTS)",
          hours: [[630, 810], null, null] }
    ]

    // Scanning is limited per meal period; dinner runs to close.
    readonly property var periods: [
        { name: "Breakfast", from: 420, until: "until 9:59am", range: [420, 600] },
        { name: "Lunch", from: 600, until: "until 3:59pm", range: [600, 960] },
        { name: "Dinner", from: 960, until: "until close", range: [960, 1440] }
    ]

    function venueDetail(venue) {
        const exchange = venue.exchange ? venue.exchange[root.dayType] : null
        if (exchange) {
            const open = venue.hours[root.dayType]
            return exchange[0] === open[0] ? "Meal exchange until " + clock(exchange[1])
                                           : "Meal exchange " + span(exchange)
        }
        return venue.note || ""
    }

    component SectionLabel: Label {
        Layout.leftMargin: 2
        color: theme.faint
        font.pixelSize: 11
        font.bold: true
        font.letterSpacing: 1.4
        elide: Text.ElideRight
    }

    // One line of a card: a name, an optional detail under it, the hours on
    // the right. `closed` greys the row out; `open` lights the gold dot in the
    // gutter, which is only there while the day shown is today.
    component HoursRow: RowLayout {
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

    // A card's heading, with the key to the gold dots while showing today.
    component CardHeading: RowLayout {
        property string text
        Layout.fillWidth: true
        Layout.bottomMargin: 12
        spacing: 6

        Label {
            Layout.fillWidth: true
            text: parent.text
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

            SectionLabel { text: "MEAL PERIODS" }

            Card {
                padding: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Repeater {
                            model: root.periods

                            Rectangle {
                                id: periodTile
                                required property var modelData
                                readonly property bool current: root.nowMinute >= modelData.range[0]
                                                                && root.nowMinute < modelData.range[1]
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                implicitHeight: periodColumn.implicitHeight + 22
                                radius: 16
                                color: current ? theme.cedarSoft : "transparent"

                                ColumnLayout {
                                    id: periodColumn
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 9
                                    anchors.rightMargin: 4
                                    spacing: 3

                                    Label {
                                        Layout.fillWidth: true
                                        text: periodTile.modelData.name.toUpperCase()
                                        color: periodTile.current ? theme.cedar : theme.muted
                                        font.pixelSize: 10
                                        font.bold: true
                                        font.letterSpacing: 0.4
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: root.clock(periodTile.modelData.from)
                                        color: theme.text
                                        font.pixelSize: 20
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: periodTile.modelData.until
                                        color: periodTile.current ? theme.cedar : theme.faint
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // How often the card scans in one period, for this plan.
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 2
                Layout.rightMargin: 2
                Layout.topMargin: -4
                text: dining.scansPerPeriod === 1
                      ? "You can scan ONCE per meal period."
                      : dining.scansPerPeriod === 5
                      ? "You can scan up to FIVE times per meal peroid."
                      : "14/21-meal plans scan once per meal period. Block plans scan up to five times."
                color: theme.faint
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 10

                SectionLabel {
                    Layout.fillWidth: true
                    text: "HOURS"
                }

                SegmentedControl {
                    implicitWidth: 204
                    implicitHeight: 36
                    fontSize: 12
                    labels: ["Mon–Fri", "Sat", "Sun"]
                    currentIndex: root.dayType
                    onActivated: (index) => root.dayType = index
                }
            }

            Card {
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    CardHeading { text: "THE COMMONS" }

                    Repeater {
                        model: root.commons[root.dayType]

                        HoursRow {
                            required property var modelData
                            required property int index
                            first: index === 0
                            name: modelData.name
                            hours: root.span(modelData.hours)
                            open: root.isNow(modelData.hours)
                        }
                    }
                }
            }

            Card {
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    CardHeading { text: "AROUND CAMPUS" }

                    Repeater {
                        model: root.venues

                        HoursRow {
                            required property var modelData
                            required property int index
                            readonly property var today: modelData.hours[root.dayType]
                            first: index === 0
                            name: modelData.name
                            closed: today === null
                            hours: today === null ? "Closed" : root.span(today)
                            detail: today === null ? "" : root.venueDetail(modelData)
                            open: root.isNow(today)
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 2
                Layout.rightMargin: 2
                Layout.topMargin: 2
                text: "Hours are for the academic year. Venues close or change hours during breaks."
                color: theme.faint
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }

            Item { Layout.preferredHeight: 8 }
        }
    }
}
