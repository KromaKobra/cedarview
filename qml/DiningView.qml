// The Dining tab: every meal scan and flex transaction on the card, newest
// first, grouped by day.
//
// Binds to `dining.activity` (a flat model of day headers and rows) and to the
// filter, `dining.flexOnly`. The filtering, the day grouping and the summary
// line are all done in DiningViewModel, so this file only decides how a row
// looks. The menu is not here: it gets a tab of its own.
//
// Colours follow the summary screen's meal-plan section: violet is flex money
// (the Temporary Flex tile's dot), orange is a meal off the plan (the meals-left
// figure). A swipe shows no amount, because it moves no money, and a column of
// "$0.00" would bury the rows that did.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    Theme { id: theme }

    Flickable {
        id: scroll
        anchors.fill: parent
        contentHeight: column.implicitHeight + theme.pageMargin * 2
        clip: true
        boundsBehavior: Flickable.DragOverBounds

        // Pull to refresh, as on the summary screen.
        onDragEnded: {
            if (contentY < -80) {
                dining.refreshPlan()
            }
        }

        ColumnLayout {
            id: column
            x: theme.pageMargin
            y: theme.pageMargin
            width: scroll.width - theme.pageMargin * 2
            spacing: theme.gap

            Card {
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            text: "RECENT ACTIVITY"
                            color: theme.muted
                            font.pixelSize: 11
                            font.bold: true
                            font.letterSpacing: 1.2
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            Layout.alignment: Qt.AlignVCenter
                            text: "Flex only"
                            color: dining.flexOnly ? theme.text : theme.faint
                            font.pixelSize: 12
                        }

                        ToggleSwitch {
                            Layout.alignment: Qt.AlignVCenter
                            on: dining.flexOnly
                            onClicked: dining.setFlexOnly(!dining.flexOnly)
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        visible: text.length > 0
                        text: dining.activitySummary
                        color: theme.muted
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.topMargin: 18
                        Layout.bottomMargin: 4
                        visible: text.length > 0
                        text: dining.activityEmptyText
                        color: theme.muted
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        model: dining.activity

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            // ---- A day ----------------------------------
                            Label {
                                Layout.fillWidth: true
                                Layout.topMargin: 20
                                visible: model.isHeader
                                text: model.title.toUpperCase()
                                color: theme.faint
                                font.pixelSize: 11
                                font.bold: true
                                font.letterSpacing: 1.1
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.topMargin: 8
                                Layout.bottomMargin: 2
                                visible: model.isHeader
                                height: 1
                                color: theme.divider
                            }

                            // ---- A transaction --------------------------
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: 12
                                visible: !model.isHeader
                                spacing: 12

                                Rectangle {
                                    Layout.alignment: Qt.AlignTop
                                    Layout.topMargin: 6
                                    width: 7
                                    height: 7
                                    radius: 3.5
                                    color: model.isFlex ? theme.violet : theme.accent
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Label {
                                        Layout.fillWidth: true
                                        text: model.title
                                        color: theme.text
                                        font.pixelSize: 14
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: text.length > 0
                                        text: model.detail
                                        color: theme.muted
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                }

                                Label {
                                    Layout.alignment: Qt.AlignTop
                                    visible: text.length > 0
                                    text: model.amount
                                    color: model.isDeposit ? theme.violet : theme.text
                                    font.pixelSize: 14
                                    font.bold: true
                                }
                            }
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: 4 }
        }
    }
}
