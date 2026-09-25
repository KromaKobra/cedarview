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
        onDragEnded: if (contentY < -80) chapel.refreshAll()

        ColumnLayout {
            id: column
            x: theme.pageMargin
            y: theme.pageMargin
            width: scroll.width - theme.pageMargin * 2
            spacing: theme.gap

            // Attendance is personal and actionable, so it leads the screen.
            Card {
                padding: 0

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: attendanceContent.implicitHeight + 40
                    radius: theme.cardRadius
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: theme.heroStart }
                        GradientStop { position: 1.0; color: theme.heroEnd }
                    }

                    ColumnLayout {
                        id: attendanceContent
                        x: 20
                        y: 20
                        width: parent.width - 40
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                text: "ATTENDANCE"
                                color: theme.heroAccent
                                font.pixelSize: 11
                                font.bold: true
                                font.letterSpacing: 1.4
                            }

                            Item { Layout.fillWidth: true }

                            Rectangle {
                                visible: chapel.loaded
                                implicitWidth: standingLabel.implicitWidth + 18
                                implicitHeight: 25
                                radius: 13
                                color: chapel.inGoodStanding
                                       ? theme.heroAccentSoft
                                       : Qt.rgba(1, 0.42, 0.39, 0.15)

                                Label {
                                    id: standingLabel
                                    anchors.centerIn: parent
                                    text: chapel.inGoodStanding ? "GOOD STANDING" : "NEEDS ATTENTION"
                                    color: chapel.inGoodStanding ? theme.heroAccent : theme.heroDanger
                                    font.pixelSize: 9
                                    font.bold: true
                                    font.letterSpacing: 0.7
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 17
                            spacing: 10

                            Label {
                                text: chapel.remaining >= 0 ? chapel.remaining : "—"
                                color: theme.textOnDark
                                font.pixelSize: 48
                                font.bold: true
                            }

                            ColumnLayout {
                                Layout.alignment: Qt.AlignBottom
                                Layout.bottomMargin: 8
                                spacing: 1

                                Label {
                                    text: "skips left"
                                    color: theme.textOnDark
                                    font.pixelSize: 14
                                    font.bold: true
                                }
                                Label {
                                    visible: chapel.allowed >= 0
                                    text: "of " + chapel.allowed + " this semester"
                                    color: Qt.rgba(0.96, 0.98, 1.0, 0.64)
                                    font.pixelSize: 11
                                }
                            }

                            Item { Layout.fillWidth: true }

                            ColumnLayout {
                                Layout.alignment: Qt.AlignBottom
                                Layout.bottomMargin: 8
                                spacing: 1
                                Label {
                                    Layout.alignment: Qt.AlignRight
                                    text: chapel.used >= 0 ? chapel.used : "—"
                                    color: theme.heroAccent
                                    font.pixelSize: 22
                                    font.bold: true
                                }
                                Label {
                                    text: "used"
                                    color: Qt.rgba(0.96, 0.98, 1.0, 0.56)
                                    font.pixelSize: 10
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: 13
                            height: 7
                            radius: 4
                            color: Qt.rgba(1, 1, 1, 0.11)

                            Rectangle {
                                width: chapel.remaining >= 0 && chapel.allowed > 0
                                       ? Math.max(parent.height,
                                                  parent.width * chapel.remainingFraction) : 0
                                height: parent.height
                                radius: 4
                                color: theme.heroAccent
                                Behavior on width {
                                    NumberAnimation { duration: 320; easing.type: Easing.OutCubic }
                                }
                            }
                        }
                    }
                }
            }

            Label {
                Layout.leftMargin: 2
                Layout.topMargin: 8
                text: "UPCOMING"
                color: theme.faint
                font.pixelSize: 11
                font.bold: true
                font.letterSpacing: 1.4
            }

            Card {
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: chapel.scheduleEmptyText
                        color: theme.muted
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        model: chapel.schedule

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Label {
                                Layout.fillWidth: true
                                Layout.topMargin: model.index === 0 ? 0 : 22
                                visible: model.isHeader
                                text: model.heading.toUpperCase()
                                color: theme.cedar
                                font.pixelSize: 10
                                font.bold: true
                                font.letterSpacing: 1.2
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.topMargin: 8
                                visible: model.isHeader
                                height: 1
                                color: theme.divider
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: 16
                                visible: !model.isHeader
                                spacing: 14

                                Rectangle {
                                    Layout.alignment: Qt.AlignTop
                                    width: 46
                                    height: 52
                                    radius: 15
                                    color: model.isNow ? theme.accentSoft : theme.cardAlt
                                    border.width: model.isNow ? 1 : 0
                                    border.color: theme.accent

                                    Column {
                                        anchors.centerIn: parent
                                        spacing: 0
                                        Label {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: model.dayName
                                            color: model.isNow ? theme.accent : theme.faint
                                            font.pixelSize: 9
                                            font.bold: true
                                            font.letterSpacing: 0.8
                                        }
                                        Label {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: model.dayNumber
                                            color: theme.text
                                            font.pixelSize: 20
                                            font.bold: true
                                        }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3

                                    RowLayout {
                                        Layout.fillWidth: true

                                        Label {
                                            Layout.fillWidth: true
                                            text: model.who
                                            color: theme.text
                                            font.pixelSize: 15
                                            font.bold: true
                                            wrapMode: Text.Wrap
                                        }

                                        Rectangle {
                                            visible: model.badge.length > 0
                                            implicitWidth: chapelBadge.implicitWidth + 14
                                            implicitHeight: 20
                                            radius: 10
                                            color: model.isNow ? theme.accent : theme.cedarSoft
                                            Label {
                                                id: chapelBadge
                                                anchors.centerIn: parent
                                                text: model.badge.toUpperCase()
                                                color: model.isNow ? theme.textOnAccent : theme.cedar
                                                font.pixelSize: 9
                                                font.bold: true
                                            }
                                        }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: text.length > 0
                                        text: model.subtitle
                                        color: theme.muted
                                        font.pixelSize: 12
                                        wrapMode: Text.Wrap
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        Layout.topMargin: 2
                                        visible: text.length > 0
                                        text: model.description
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

            Label {
                Layout.leftMargin: 2
                Layout.topMargin: 8
                text: "SKIP HISTORY"
                color: theme.faint
                font.pixelSize: 11
                font.bold: true
                font.letterSpacing: 1.4
            }

            Card {
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        visible: chapel.loaded && history.count === 0
                        text: "No skip activity this semester."
                        color: theme.muted
                        font.pixelSize: 13
                    }

                    Repeater {
                        id: history
                        model: chapel.records

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: model.index === 0 ? 0 : 15
                            spacing: 12

                            Rectangle {
                                width: 36
                                height: 36
                                radius: 12
                                color: model.isSkip ? theme.accentSoft : theme.cedarSoft

                                Label {
                                    anchors.centerIn: parent
                                    text: model.count > 0 ? "+" + model.count : model.count
                                    color: model.isSkip ? theme.accent : theme.cedar
                                    font.pixelSize: 13
                                    font.bold: true
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    Layout.fillWidth: true
                                    text: model.whenText
                                    color: theme.text
                                    font.pixelSize: 13
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: text.length > 0
                                    text: model.reason
                                    color: theme.muted
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }

                            Label {
                                text: model.entryType
                                color: theme.faint
                                font.pixelSize: 10
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: chapel.error.length > 0
                implicitHeight: chapelError.implicitHeight + 30
                radius: 16
                color: theme.dangerSoft
                Label {
                    id: chapelError
                    anchors.fill: parent
                    anchors.margins: 15
                    text: chapel.error
                    color: theme.danger
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
            }

            Item { Layout.preferredHeight: 8 }
        }
    }
}
