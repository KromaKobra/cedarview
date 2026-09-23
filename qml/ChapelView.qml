// The Chapel tab: the chapel happening now, if any, and every one after it,
// grouped by week.
//
// Binds to `chapel.schedule` (a flat model of week headers and chapels) and
// `chapel.scheduleEmptyText`. What counts as "now" and which week a chapel
// falls in are decided in ChapelViewModel, off the clock, so this file only
// decides how a row looks. The schedule comes from the public media feed, so
// this screen fills in before sign-in.
//
// Still to come here: the skip ledger (`chapel.records`) — *which* chapels you
// missed, to go with the count on the summary screen.

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

        // Pull to refresh, as on the other tabs.
        onDragEnded: {
            if (contentY < -80) {
                chapel.refreshAll()
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

                    Label {
                        text: "UPCOMING CHAPELS"
                        color: theme.muted
                        font.pixelSize: 11
                        font.bold: true
                        font.letterSpacing: 1.2
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.topMargin: 18
                        Layout.bottomMargin: 4
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

                            // ---- A week ---------------------------------
                            Label {
                                Layout.fillWidth: true
                                Layout.topMargin: 20
                                visible: model.isHeader
                                text: model.heading.toUpperCase()
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

                            // ---- A chapel -------------------------------
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: 14
                                visible: !model.isHeader
                                spacing: 14

                                // The date, calendar-style. Fixed width so the
                                // names line up down the list whether the day
                                // is "1" or "30".
                                ColumnLayout {
                                    Layout.alignment: Qt.AlignTop
                                    Layout.preferredWidth: 38
                                    spacing: 0

                                    Label {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: model.dayName
                                        color: model.isNow ? theme.accent : theme.faint
                                        font.pixelSize: 10
                                        font.bold: true
                                        font.letterSpacing: 1.0
                                    }

                                    Label {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: model.dayNumber
                                        color: model.isNow ? theme.accent : theme.text
                                        font.pixelSize: 22
                                        font.bold: true
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        Label {
                                            Layout.fillWidth: true
                                            text: model.who
                                            color: theme.text
                                            font.pixelSize: 15
                                            font.bold: true
                                            wrapMode: Text.Wrap
                                        }

                                        // "Now" is filled with the accent, the
                                        // day badges only tinted with it: the
                                        // one happening this minute should be
                                        // the thing the eye lands on.
                                        Rectangle {
                                            Layout.alignment: Qt.AlignTop
                                            visible: model.badge.length > 0
                                            implicitWidth: badgeLabel.implicitWidth + 16
                                            implicitHeight: 20
                                            radius: 10
                                            color: model.isNow ? theme.accent : theme.accentSoft

                                            Label {
                                                id: badgeLabel
                                                anchors.centerIn: parent
                                                text: model.badge.toUpperCase()
                                                color: model.isNow ? theme.card : theme.accent
                                                font.pixelSize: 10
                                                font.bold: true
                                                font.letterSpacing: 0.8
                                            }
                                        }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: text.length > 0
                                        text: model.subtitle
                                        color: theme.muted
                                        font.pixelSize: 13
                                        wrapMode: Text.Wrap
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: model.timeText
                                              + (model.livestream ? "" : " · No livestream")
                                        color: theme.muted
                                        font.pixelSize: 12
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        Layout.topMargin: 2
                                        visible: text.length > 0
                                        text: model.description
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
