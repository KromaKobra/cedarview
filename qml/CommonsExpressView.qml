// Dining › Menu › The Commons Express: whether it is open and the Order
// button first, then the menu a card per category, then the week's hours —
// all read from the ordering site (`express`).

import QtQuick
import QtQuick.Layouts

ScrollPage {
    id: root

    Theme { id: theme }

    readonly property var menu: express.menuStatus

    Card {
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                radius: 12
                color: theme.goldSoft
                Glyph {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    kind: "plate"
                    color: theme.gold
                }
            }
            Text {
                Layout.fillWidth: true
                text: "The Commons Express"
                color: theme.text
                font.family: theme.display
                font.pixelSize: 18
                font.weight: Font.Bold
                elide: Text.ElideRight
            }
            StatusStamp { status: root.menu }
        }
        RowLayout {
            visible: express.hoursStatus.length > 0
            Layout.fillWidth: true
            Layout.topMargin: 10
            Layout.leftMargin: 2
            spacing: 7
            Glyph {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                kind: "clock"
                color: express.open ? theme.gold : theme.muted
            }
            Text {
                Layout.fillWidth: true
                text: express.hoursStatus
                color: express.open ? theme.gold : theme.muted
                font.family: theme.ui
                font.pixelSize: 13
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }
        PrimaryButton {
            Layout.fillWidth: true
            Layout.topMargin: 14
            glyph: "external"
            text: "Order"
            onClicked: Qt.openUrlExternally(express.orderUrl)
        }
    }

    // ---- The menu -----------------------------------------------------------------
    Repeater {
        model: express.categories

        Card {
            id: category
            required property var modelData

            Text {
                Layout.fillWidth: true
                Layout.bottomMargin: 4
                text: category.modelData.name
                color: theme.text
                font.family: theme.display
                font.pixelSize: 18
                font.weight: Font.Bold
                elide: Text.ElideRight
            }

            Repeater {
                model: category.modelData.items

                ColumnLayout {
                    id: dish
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 3

                    Rectangle {
                        visible: dish.index > 0
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        Layout.bottomMargin: 8
                        implicitHeight: 1
                        color: theme.line
                    }
                    Rectangle {
                        visible: dish.index === 0
                        Layout.preferredHeight: 6
                        color: "transparent"
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Text {
                            Layout.fillWidth: true
                            text: dish.modelData.name
                            color: theme.text
                            font.family: theme.ui
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            wrapMode: Text.Wrap
                        }
                        Text {
                            Layout.alignment: Qt.AlignTop
                            text: dish.modelData.price
                            color: theme.gold
                            font.family: theme.display
                            font.pixelSize: 15
                            font.weight: Font.Bold
                        }
                    }
                    Text {
                        visible: dish.modelData.description.length > 0
                        Layout.fillWidth: true
                        text: dish.modelData.description
                        color: theme.muted
                        font.family: theme.ui
                        font.pixelSize: 13
                        lineHeight: 1.15
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }

    // Loading with nothing to show yet: one category's worth of skeleton.
    Card {
        visible: !root.menu.hasData && root.menu.loading
        Skeleton { Layout.preferredWidth: 150; Layout.preferredHeight: 18 }
        Repeater {
            model: 3
            Skeleton {
                required property int index
                Layout.topMargin: 16
                Layout.preferredWidth: [210, 170, 190][index]
                Layout.preferredHeight: 13
            }
        }
    }

    Card {
        visible: !root.menu.hasData && !root.menu.loading && root.menu.error.length > 0
        EmptyState {
            Layout.fillWidth: true
            status: root.menu
            what: "the Commons Express menu"
        }
    }

    // ---- Hours --------------------------------------------------------------------
    SectionCaption {
        text: "Hours"
        note: express.liveHours ? "" : "Posted hours"
    }

    Card {
        Repeater {
            model: express.weekHours

            RowLayout {
                id: hoursRow
                required property var modelData
                Layout.fillWidth: true
                Layout.topMargin: 3
                Layout.bottomMargin: 3
                spacing: 12
                Text {
                    Layout.preferredWidth: 44
                    text: hoursRow.modelData.day
                    color: hoursRow.modelData.today ? theme.text : theme.muted
                    font.family: theme.ui
                    font.pixelSize: 14
                    font.weight: hoursRow.modelData.today ? Font.Bold : Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: hoursRow.modelData.hours
                    color: hoursRow.modelData.today ? theme.text : theme.muted
                    font.family: theme.ui
                    font.pixelSize: 14
                    font.weight: hoursRow.modelData.today ? Font.Bold : Font.Normal
                    horizontalAlignment: Text.AlignRight
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
