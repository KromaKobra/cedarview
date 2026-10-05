// Dining › Menu: a week of days around today, the three sittings, what to
// avoid, and the three stations worth a card each — Home Cooking, Garden
// Bites and Allergen Aware — with the all-day stations along the bottom.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollPage {
    id: root

    Theme { id: theme }

    readonly property var menu: dining.menuStatus

    // ---- The days ----------------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: 5

        Repeater {
            model: dining.days

            AbstractButton {
                id: day
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: 62
                enabled: modelData.available
                focusPolicy: Qt.NoFocus
                onClicked: dining.selectDay(modelData.offset)
                Accessible.name: modelData.dow + " " + modelData.num
                Accessible.selected: modelData.selected

                background: Rectangle {
                    radius: 16
                    color: day.modelData.selected ? theme.cedar : theme.surface
                    opacity: day.enabled ? 1 : 0.45
                    border.width: !day.modelData.selected && !theme.light ? 1 : 0
                    border.color: theme.cardHighlight
                }
                contentItem: Column {
                    spacing: 2
                    topPadding: 9
                    opacity: day.enabled ? 1 : 0.6
                    Text {
                        width: day.width
                        horizontalAlignment: Text.AlignHCenter
                        text: day.modelData.dow
                        color: day.modelData.selected ? theme.bg : theme.faint
                        font.family: theme.ui
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Text {
                        width: day.width
                        horizontalAlignment: Text.AlignHCenter
                        text: day.modelData.num
                        color: day.modelData.selected ? theme.bg : day.enabled ? theme.text : theme.faint
                        font.family: theme.display
                        font.pixelSize: 18
                        font.weight: Font.Bold
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 5
                        height: 5
                        radius: 3
                        color: day.modelData.isToday ? (day.modelData.selected ? theme.bg : theme.gold) : "transparent"
                    }
                }
            }
        }
    }

    // ---- The sittings ------------------------------------------------------------
    SegmentedControl {
        Layout.fillWidth: true
        radius: 18
        fontSize: 14
        labels: dining.mealTabs.map(tab => tab.label)
        sublabels: dining.mealTabs.map(tab => tab.hours)
        currentIndex: dining.mealTabs.findIndex(tab => tab.selected)
        onActivated: (index) => dining.selectMeal(dining.mealTabs[index].slot)
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 4
        spacing: 7
        Glyph {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            kind: "clock"
            color: dining.mealStatusLive ? theme.gold : theme.muted
        }
        Text {
            Layout.fillWidth: true
            text: dining.mealStatusText
            color: dining.mealStatusLive ? theme.gold : theme.muted
            font.family: theme.ui
            font.pixelSize: 13
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        StatusStamp { status: root.menu }
    }

    // ---- Avoid -------------------------------------------------------------------
    Flow {
        Layout.fillWidth: true
        spacing: 6

        Text {
            height: 34
            verticalAlignment: Text.AlignVCenter
            rightPadding: 2
            text: "Avoid"
            color: theme.faint
            font.family: theme.ui
            font.pixelSize: 12
            font.weight: Font.Bold
        }
        Repeater {
            model: dining.avoidOptions
            Chip {
                required property var modelData
                text: modelData.label
                interactive: true
                implicitHeight: 34
                leftPadding: 12
                rightPadding: 12
                fill: modelData.on ? theme.cedar : theme.surface
                textColor: modelData.on ? theme.bg : theme.muted
                outlined: !modelData.on
                onClicked: dining.toggleAvoid(modelData.key)
            }
        }
    }

    Rectangle {
        visible: dining.hiddenCount > 0
        Layout.fillWidth: true
        implicitHeight: hiddenRow.implicitHeight + 20
        radius: 16
        color: theme.cedarSoft

        RowLayout {
            id: hiddenRow
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 10
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: dining.hiddenText
                color: theme.cedar
                font.family: theme.ui
                font.pixelSize: 13
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
            AbstractButton {
                id: showAll
                implicitHeight: 32
                implicitWidth: showAllLabel.implicitWidth + 24
                focusPolicy: Qt.NoFocus
                onClicked: dining.clearAvoid()
                Accessible.name: "Show all"
                background: Rectangle { radius: 10; color: theme.surface; opacity: showAll.down ? 0.8 : 1 }
                contentItem: Text {
                    id: showAllLabel
                    text: "Show all"
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

    // ---- The stations -------------------------------------------------------------
    // One flat model; each row draws its slice of its station's card, the
    // header rounding the top and the last row the bottom.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0

        Repeater {
            id: stationRows
            model: dining.stations

            Item {
                id: row
                required property int index
                required property string rowType
                required property string station
                required property string stationKind
                required property string text
                required property string allergens
                required property bool isNew
                required property string hiddenText
                required property bool isLast

                readonly property bool header: rowType === "header"

                Layout.fillWidth: true
                Layout.topMargin: header && index > 0 ? theme.gap : 0
                implicitHeight: content.implicitHeight + (header ? 24 : isLast ? 22 : 22)

                Rectangle {
                    anchors.fill: parent
                    color: theme.surface
                    topLeftRadius: row.header ? theme.cardRadius : 0
                    topRightRadius: row.header ? theme.cardRadius : 0
                    bottomLeftRadius: row.isLast ? theme.cardRadius : 0
                    bottomRightRadius: row.isLast ? theme.cardRadius : 0
                }
                Rectangle {
                    visible: !row.header
                    x: 16
                    width: parent.width - 32
                    height: 1
                    color: theme.line
                }

                ColumnLayout {
                    id: content
                    x: 16
                    y: row.header ? 14 : 11
                    width: parent.width - 32
                    spacing: 3

                    RowLayout {
                        visible: row.header
                        Layout.fillWidth: true
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 36
                            radius: 12
                            color: row.stationKind === "garden" ? theme.cedarSoft
                                 : row.stationKind === "aware" ? theme.violetSoft : theme.goldSoft
                            Glyph {
                                anchors.centerIn: parent
                                width: 18
                                height: 18
                                kind: row.stationKind === "garden" ? "leaf" : row.stationKind === "aware" ? "shield" : "pot"
                                color: row.stationKind === "garden" ? theme.cedar
                                     : row.stationKind === "aware" ? theme.violet : theme.gold
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: row.station
                            color: theme.text
                            font.family: theme.display
                            font.pixelSize: 18
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                        }
                        Chip {
                            visible: row.hiddenText.length > 0
                            text: row.hiddenText
                            implicitHeight: 24
                            fontSize: 11.5
                        }
                    }

                    RowLayout {
                        visible: row.rowType === "item"
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            Layout.fillWidth: !newChip.visible
                            text: row.text
                            color: theme.text
                            font.family: theme.ui
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            wrapMode: Text.Wrap
                        }
                        Chip {
                            id: newChip
                            visible: row.isNew
                            text: "NEW"
                            implicitHeight: 20
                            leftPadding: 7
                            rightPadding: 7
                            fontSize: 10
                            fill: theme.gold
                            textColor: theme.bg
                        }
                        Item { Layout.fillWidth: newChip.visible }
                    }
                    Text {
                        visible: row.rowType === "item" && row.allergens.length > 0
                        Layout.fillWidth: true
                        text: "Contains " + row.allergens
                        color: theme.faint
                        font.family: theme.ui
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                    Text {
                        visible: row.rowType === "extras" || row.rowType === "note"
                        Layout.fillWidth: true
                        text: row.text
                        color: theme.muted
                        font.family: theme.ui
                        font.pixelSize: row.rowType === "note" ? 13 : 12.5
                        lineHeight: 1.15
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }

    // Loading with nothing to show yet: one station's worth of skeleton.
    Card {
        visible: stationRows.count === 0 && dining.dayLoading
        RowLayout {
            spacing: 10
            Skeleton { Layout.preferredWidth: 36; Layout.preferredHeight: 36; radius: 12 }
            Skeleton { Layout.preferredWidth: 130; Layout.preferredHeight: 18 }
        }
        Repeater {
            model: 4
            Skeleton {
                required property int index
                Layout.topMargin: 16
                Layout.preferredWidth: [200, 160, 220, 140][index]
                Layout.preferredHeight: 13
            }
        }
    }

    Card {
        visible: stationRows.count === 0 && !dining.dayLoading
        Text {
            Layout.fillWidth: true
            text: dining.dayEmptyText
            color: theme.muted
            font.family: theme.ui
            font.pixelSize: 14
            wrapMode: Text.Wrap
        }
        EmptyState {
            Layout.fillWidth: true
            Layout.topMargin: 10
            status: root.menu
            what: "the menu"
        }
    }

    // ---- All day ----------------------------------------------------------------
    SectionCaption {
        visible: dining.allDayStations.length > 0
        text: "All-day stations"
        note: dining.allDayStations.length + (dining.allDayStations.length === 1 ? " station" : " stations")
    }

    ListView {
        visible: dining.allDayStations.length > 0
        Layout.fillWidth: true
        Layout.preferredHeight: 118
        orientation: ListView.Horizontal
        spacing: 10
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: dining.allDayStations

        delegate: Card {
            id: allDay
            required property var modelData
            width: 150
            height: 112
            radius: 20
            padding: 14

            Text {
                Layout.fillWidth: true
                text: allDay.modelData.name
                color: theme.text
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.Bold
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 4
                text: allDay.modelData.text
                color: theme.muted
                font.family: theme.ui
                font.pixelSize: 12
                lineHeight: 1.1
                wrapMode: Text.Wrap
                maximumLineCount: 4
                elide: Text.ElideRight
            }
        }
    }
}
