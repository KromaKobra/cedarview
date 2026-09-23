// The Chucks tab: Home Cooking's whole day — breakfast, lunch and dinner — for
// any date, paged a day at a time.
//
// Binds to `dining.items` (a flat model of meal headers and dishes) and to the
// paging state on DiningViewModel. Which days are fetched, and when, is decided
// there: this file only draws the day it is given. Days are paged with buttons
// rather than a swipe, because a horizontal swipe already belongs to the
// SwipeView and means "next tab".
//
// There is no end to page to. Which days Cedarville has posted is not known
// until they are asked for, so every day is reachable and a day with nothing on
// it says so — `dining.dayEmptyText` carries that message.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    Theme { id: theme }

    // One round arrow. The glyph is wrapped in an Item for the same reason as
    // the ribbon's refresh button in Main.qml: Control positions its
    // contentItem at the padding, and an explicitly sized Glyph would sit in
    // the top-left corner instead of the middle.
    component DayButton: AbstractButton {
        id: dayButton

        property string kind

        implicitWidth: 44
        implicitHeight: 44

        background: Rectangle {
            radius: width / 2
            color: dayButton.down ? theme.pressedStrong : theme.pressed
        }

        contentItem: Item {
            Glyph {
                anchors.centerIn: parent
                kind: dayButton.kind
                color: theme.text
                width: 16
                height: 16
            }
        }
    }

    Flickable {
        id: scroll
        anchors.fill: parent
        contentHeight: column.implicitHeight + theme.pageMargin * 2
        clip: true
        boundsBehavior: Flickable.DragOverBounds

        // Pull to refresh, as on the other tabs.
        onDragEnded: {
            if (contentY < -80) {
                dining.refreshAll()
            }
        }

        ColumnLayout {
            id: column
            x: theme.pageMargin
            y: theme.pageMargin
            width: scroll.width - theme.pageMargin * 2
            spacing: theme.gap

            // ---- Which day ---------------------------------------------------
            Card {
                padding: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    DayButton {
                        Layout.alignment: Qt.AlignVCenter
                        kind: "chevronLeft"
                        onClicked: dining.previousDay()
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: dining.dateText
                            color: theme.text
                            font.pixelSize: 20
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: dining.dateDetail
                            color: theme.muted
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }

                    DayButton {
                        Layout.alignment: Qt.AlignVCenter
                        kind: "chevronRight"
                        onClicked: dining.nextDay()
                    }
                }

                // Only once you have left today: it is the way home after
                // paging a month into the past, and on today it would be a
                // button that does nothing.
                AbstractButton {
                    id: todayButton
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 8
                    visible: !dining.isToday
                    onClicked: dining.goToToday()

                    implicitWidth: todayLabel.implicitWidth + 28
                    implicitHeight: 28

                    background: Rectangle {
                        radius: height / 2
                        color: todayButton.down ? theme.pressedStrong : theme.accentSoft
                    }

                    contentItem: Item {
                        Label {
                            id: todayLabel
                            anchors.centerIn: parent
                            text: "Back to today"
                            color: theme.accent
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }
                }
            }

            // ---- The day's menu ----------------------------------------------
            Card {
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            text: dining.venue.toUpperCase()
                            color: theme.muted
                            font.pixelSize: 11
                            font.bold: true
                            font.letterSpacing: 1.2
                        }

                        Item { Layout.fillWidth: true }

                        BusyIndicator {
                            Layout.alignment: Qt.AlignVCenter
                            running: dining.dayLoading
                            visible: running
                            implicitWidth: 18
                            implicitHeight: 18
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.topMargin: 18
                        Layout.bottomMargin: 4
                        visible: text.length > 0
                        text: dining.dayEmptyText
                        color: theme.muted
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        model: dining.items

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            // ---- A sitting ------------------------------
                            // Styled as the summary card's meal label, so
                            // "BREAKFAST" means the same thing on both screens.
                            Label {
                                Layout.fillWidth: true
                                Layout.topMargin: 20
                                visible: model.isHeader
                                text: model.text.toUpperCase()
                                color: theme.accent
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

                            // ---- A dish ---------------------------------
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: 12
                                visible: !model.isHeader
                                spacing: 12

                                Rectangle {
                                    Layout.alignment: Qt.AlignTop
                                    Layout.topMargin: 6
                                    width: 5
                                    height: 5
                                    radius: 2.5
                                    color: theme.faint
                                }

                                // Allergens go here and not on the summary
                                // card: that card is a glance, this is where
                                // you check what is actually in the food.
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Label {
                                        Layout.fillWidth: true
                                        text: model.text
                                        color: theme.text
                                        font.pixelSize: 14
                                        wrapMode: Text.Wrap
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: text.length > 0
                                        text: model.allergens
                                        color: theme.faint
                                        font.pixelSize: 12
                                        wrapMode: Text.Wrap
                                    }
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
