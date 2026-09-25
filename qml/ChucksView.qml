import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    Theme { id: theme }

    component DayButton: AbstractButton {
        id: dayButton
        property string kind
        implicitWidth: 44
        implicitHeight: 44
        background: Rectangle {
            radius: 15
            color: dayButton.down ? theme.pressedStrong : theme.cardAlt
            border.width: 1
            border.color: theme.cardBorder
        }
        contentItem: Item {
            Glyph {
                anchors.centerIn: parent
                kind: dayButton.kind
                color: theme.text
                width: 15
                height: 15
            }
        }
    }

    Flickable {
        id: scroll
        anchors.fill: parent
        contentHeight: column.implicitHeight + theme.pageMargin * 2
        clip: true
        boundsBehavior: Flickable.DragOverBounds
        onDragEnded: if (contentY < -80) dining.refreshAll()

        ColumnLayout {
            id: column
            x: theme.pageMargin
            y: theme.pageMargin
            width: scroll.width - theme.pageMargin * 2
            spacing: theme.gap

            Card {
                padding: 14

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        DayButton { kind: "chevronLeft"; onClicked: dining.previousDay() }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                Layout.fillWidth: true
                                text: dining.dateText
                                color: theme.text
                                font.pixelSize: 21
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: dining.dateDetail
                                color: theme.muted
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }
                        }

                        DayButton { kind: "chevronRight"; onClicked: dining.nextDay() }
                    }

                    AbstractButton {
                        id: todayButton
                        Layout.alignment: Qt.AlignHCenter
                        visible: !dining.isToday
                        implicitWidth: todayText.implicitWidth + 26
                        implicitHeight: visible ? 30 : 0
                        onClicked: dining.goToToday()
                        background: Rectangle {
                            radius: 15
                            color: todayButton.down ? theme.pressedStrong : theme.cedarSoft
                        }
                        contentItem: Label {
                            id: todayText
                            text: "Return to today"
                            color: theme.cedar
                            font.pixelSize: 11
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 2
                Layout.topMargin: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        text: dining.venue.toUpperCase()
                        color: theme.faint
                        font.pixelSize: 11
                        font.bold: true
                        font.letterSpacing: 1.4
                    }
                    Label {
                        text: "Breakfast, lunch, and dinner"
                        color: theme.muted
                        font.pixelSize: 11
                    }
                }

                Rectangle {
                    visible: dining.dayLoading
                    width: 34
                    height: 34
                    radius: 17
                    color: theme.cedarSoft
                    BusyIndicator {
                        anchors.centerIn: parent
                        running: parent.visible
                        implicitWidth: 20
                        implicitHeight: 20
                    }
                }
            }

            Card {
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: dining.dayEmptyText.length > 0
                        spacing: 8

                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            width: 52
                            height: 52
                            radius: 18
                            color: theme.cardAlt
                            Glyph {
                                anchors.centerIn: parent
                                kind: "chucks"
                                color: theme.faint
                                width: 23
                                height: 23
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: dining.dayEmptyText
                            color: theme.muted
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                        }
                    }

                    Repeater {
                        model: dining.items

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.topMargin: model.index === 0 ? 0 : 22
                                visible: model.isHeader
                                implicitHeight: 42
                                radius: 14
                                color: theme.cedarSoft

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 13
                                    anchors.rightMargin: 13
                                    spacing: 10

                                    Rectangle {
                                        width: 8
                                        height: 8
                                        radius: 4
                                        color: theme.cedar
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: model.text.toUpperCase()
                                        color: theme.cedar
                                        font.pixelSize: 10
                                        font.bold: true
                                        font.letterSpacing: 1.0
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        visible: text.length > 0
                                        text: model.hours
                                        color: theme.cedar
                                        font.pixelSize: 11
                                        font.bold: true
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: 14
                                visible: !model.isHeader
                                spacing: 12

                                Rectangle {
                                    Layout.alignment: Qt.AlignTop
                                    Layout.topMargin: 5
                                    width: 6
                                    height: 6
                                    radius: 3
                                    color: theme.accent
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Label {
                                        Layout.fillWidth: true
                                        text: model.text
                                        color: theme.text
                                        font.pixelSize: 13
                                        font.bold: true
                                        wrapMode: Text.Wrap
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        visible: text.length > 0
                                        text: model.allergens
                                        color: theme.faint
                                        font.pixelSize: 11
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: 8 }
        }
    }
}
