// Home Cooking, every meal, for one day. Binds to the `dining` context
// property (mycu.ui.viewmodels.dining.DiningViewModel).
//
// The model is flat: meal headers and dishes in one list, tagged with
// `isHeader`. That keeps section headings scrolling with their items, which is
// the behaviour you want on a phone.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Day pager ------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: Qt.rgba(0, 0, 0, 0.04)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8

                ToolButton {
                    text: "‹"
                    font.pixelSize: 22
                    enabled: dining.canGoBack
                    opacity: enabled ? 1 : 0.25
                    onClicked: dining.previousDay()
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        text: dining.dateText
                        font.pixelSize: 17
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: dining.venue
                        font.pixelSize: 12
                        opacity: 0.65
                        Layout.alignment: Qt.AlignHCenter
                    }
                }

                ToolButton {
                    text: "›"
                    font.pixelSize: 22
                    enabled: dining.canGoForward
                    opacity: enabled ? 1 : 0.25
                    onClicked: dining.nextDay()
                }
            }
        }

        // ---- Meal plan ------------------------------------------------------
        // Behind the Cedarville sign-in, unlike the menu below it. Hidden
        // entirely until it loads: an empty row of "$0.00"s would misstate
        // money, which is the one thing this panel must never do.
        //
        // The two dollar figures are deliberately shown separately and with
        // their expiry spelled out. They are different balances — dining
        // dollars expire at the end of term, voluntary flex dollars do not —
        // and collapsing them into one number would be wrong.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: planRow.implicitHeight + 24
            visible: dining.hasPlan
            color: Qt.rgba(0, 0, 0, 0.02)

            RowLayout {
                id: planRow
                anchors.centerIn: parent
                width: parent.width - 32
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 1
                    visible: dining.mealsRemaining >= 0

                    Label {
                        text: dining.mealsRemaining
                        font.pixelSize: 22
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: dining.mealsRemaining === 1 ? "meal left" : "meals left"
                        font.pixelSize: 11
                        opacity: 0.6
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: "this week"
                        font.pixelSize: 10
                        opacity: 0.45
                        Layout.alignment: Qt.AlignHCenter
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 1
                    visible: dining.diningDollars.length > 0

                    Label {
                        text: dining.diningDollars
                        font.pixelSize: 22
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: "dining dollars"
                        font.pixelSize: 11
                        opacity: 0.6
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: "expire end of term"
                        font.pixelSize: 10
                        color: "#b45309"
                        opacity: 0.8
                        Layout.alignment: Qt.AlignHCenter
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 1
                    visible: dining.flexDollars.length > 0

                    Label {
                        text: dining.flexDollars
                        font.pixelSize: 22
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: "flex dollars"
                        font.pixelSize: 11
                        opacity: 0.6
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: "never expire"
                        font.pixelSize: 10
                        opacity: 0.45
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Qt.rgba(0, 0, 0, 0.08)
            }
        }

        // ---- Error ----------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: diningError.implicitHeight + 24
            visible: dining.error.length > 0
            color: "#fdecea"

            Label {
                id: diningError
                anchors.centerIn: parent
                width: parent.width - 32
                wrapMode: Text.Wrap
                color: "#7f1d1d"
                font.pixelSize: 13
                text: dining.error
            }
        }

        // ---- Menu -----------------------------------------------------------
        ListView {
            id: menuList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: dining.items

            delegate: Item {
                width: menuList.width
                height: model.isHeader ? 44 : (model.allergens.length > 0 ? 52 : 38)

                // Meal header — Breakfast / Lunch / Dinner
                Rectangle {
                    anchors.fill: parent
                    visible: model.isHeader
                    color: Qt.rgba(0, 0, 0, 0.03)

                    Label {
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: model.text
                        font.pixelSize: 14
                        font.bold: true
                        font.capitalization: Font.AllUppercase
                        opacity: 0.7
                    }
                }

                // Dish
                ColumnLayout {
                    visible: !model.isHeader
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 20
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1

                    Label {
                        text: model.text
                        font.pixelSize: 15
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Label {
                        visible: model.allergens.length > 0
                        text: model.allergens
                        font.pixelSize: 11
                        opacity: 0.55
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 64
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                opacity: 0.6
                visible: menuList.count === 0 && !dining.busy && dining.error.length === 0
                text: dining.loaded
                      ? "Nothing listed for " + dining.venue + " on this day."
                      : "Pull to refresh."
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: dining.busy
                visible: running && menuList.count === 0
            }

            onDragEnded: {
                if (contentY < -80 && !dining.busy) {
                    dining.refresh()
                }
            }
        }
    }
}
