import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The glanceable home screen. It answers the three questions students most
// often open myCU for: what is next, what can I spend, and what is being served.
Item {
    id: root
    signal openTab(int index)

    Theme { id: theme }

    Flickable {
        id: scroll
        anchors.fill: parent
        contentHeight: column.implicitHeight + theme.pageMargin * 2
        clip: true
        boundsBehavior: Flickable.DragOverBounds

        onDragEnded: {
            if (contentY < -80 && !chapel.busy && !dining.busy) {
                chapel.refreshAll()
                dining.refreshAll()
                semester.refreshAll()
            }
        }

        ColumnLayout {
            id: column
            x: theme.pageMargin
            y: theme.pageMargin
            width: scroll.width - theme.pageMargin * 2
            spacing: theme.gap

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 2
                Layout.rightMargin: 2
                Layout.bottomMargin: 2
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        text: "YOUR DAY"
                        color: theme.cedar
                        font.pixelSize: 11
                        font.bold: true
                        font.letterSpacing: 1.5
                    }

                    Label {
                        text: "The essentials, without the hunt."
                        color: theme.muted
                        font.pixelSize: 13
                    }
                }

                Rectangle {
                    visible: chapel.busy || dining.busy
                    implicitWidth: 34
                    implicitHeight: 34
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

            // The next commitment is the visual anchor. The date gets a
            // compact badge while the speaker remains the strongest type.
            Card {
                padding: 0
                tappable: true
                onTapped: root.openTab(1)

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: heroContent.implicitHeight + 40
                    radius: theme.cardRadius
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: theme.heroStart }
                        GradientStop { position: 1.0; color: theme.heroEnd }
                    }

                    Rectangle {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.rightMargin: -44
                        anchors.topMargin: -64
                        width: 184
                        height: 184
                        radius: 92
                        color: Qt.rgba(1, 1, 1, 0.045)
                    }

                    ColumnLayout {
                        id: heroContent
                        x: 20
                        y: 20
                        width: parent.width - 40
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                text: "NEXT CHAPEL"
                                color: theme.heroAccent
                                font.pixelSize: 11
                                font.bold: true
                                font.letterSpacing: 1.4
                            }

                            Item { Layout.fillWidth: true }

                            Rectangle {
                                visible: chapel.nextChapelDay.length > 0
                                implicitWidth: heroDay.implicitWidth + 20
                                implicitHeight: 25
                                radius: 13
                                color: Qt.rgba(1, 1, 1, 0.10)
                                border.width: 1
                                border.color: Qt.rgba(1, 1, 1, 0.12)

                                Label {
                                    id: heroDay
                                    anchors.centerIn: parent
                                    text: chapel.nextChapelDay.toUpperCase()
                                    color: theme.textOnDark
                                    font.pixelSize: 10
                                    font.bold: true
                                    font.letterSpacing: 1.0
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.topMargin: 18
                            text: chapel.nextSpeaker.length > 0
                                  ? chapel.nextSpeaker : "No chapel scheduled"
                            color: theme.textOnDark
                            font.pixelSize: 30
                            font.bold: true
                            wrapMode: Text.Wrap
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.topMargin: 7
                            visible: text.length > 0
                            text: chapel.nextChapelDateText
                                  + (chapel.nextChapelTitle.length > 0
                                     ? "  ·  " + chapel.nextChapelTitle : "")
                            color: Qt.rgba(0.96, 0.98, 1.0, 0.70)
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: 20
                            implicitHeight: 60
                            radius: 17
                            color: Qt.rgba(0, 0, 0, 0.16)

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                anchors.rightMargin: 14
                                spacing: 10

                                ColumnLayout {
                                    spacing: 0

                                    Label {
                                        text: chapel.remaining >= 0 ? chapel.remaining : "—"
                                        color: theme.heroAccent
                                        font.pixelSize: 26
                                        font.bold: true
                                    }

                                    Label {
                                        text: "skips remaining"
                                        color: Qt.rgba(0.96, 0.98, 1.0, 0.68)
                                        font.pixelSize: 10
                                    }
                                }

                                Item { Layout.fillWidth: true }

                                Label {
                                    text: chapel.loaded && !chapel.inGoodStanding
                                          ? "Needs attention" : "View schedule"
                                    color: chapel.loaded && !chapel.inGoodStanding
                                           ? theme.heroDanger : theme.textOnDark
                                    font.pixelSize: 12
                                    font.bold: true
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 5

                Label {
                    text: "MEAL CARD"
                    color: theme.faint
                    font.pixelSize: 11
                    font.bold: true
                    font.letterSpacing: 1.4
                }

                Item { Layout.fillWidth: true }

                AbstractButton {
                    id: cardDetails
                    onClicked: root.openTab(2)
                    implicitWidth: detailsLabel.implicitWidth + 20
                    implicitHeight: 28
                    background: Rectangle {
                        radius: 14
                        color: cardDetails.down ? theme.pressedStrong : theme.cedarSoft
                    }
                    contentItem: Label {
                        id: detailsLabel
                        text: "See activity"
                        color: theme.cedar
                        font.pixelSize: 11
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // Three independent balances. Temporary and permanent flex are
            // deliberately kept separate because they expire differently.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Repeater {
                    model: [
                        { label: "MEALS", value: dining.mealsRemaining >= 0 ? dining.mealsRemaining : "—",
                          note: dining.mealsPeriodText, tone: theme.accent },
                        { label: "TEMP FLEX", value: dining.diningDollars.length > 0 ? dining.diningDollars : "—",
                          note: "Expires", tone: theme.violet },
                        { label: "PERM FLEX", value: dining.flexDollars.length > 0 ? dining.flexDollars : "—",
                          note: "Rolls over", tone: theme.cedar }
                    ]

                    Card {
                        required property var modelData
                        Layout.preferredWidth: 1
                        padding: 13
                        tappable: true
                        onTapped: root.openTab(2)

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Rectangle {
                                width: 8
                                height: 8
                                radius: 4
                                color: modelData.tone
                            }

                            Label {
                                Layout.fillWidth: true
                                Layout.topMargin: 11
                                text: modelData.value
                                color: theme.text
                                font.pixelSize: 17
                                font.bold: true
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                text: modelData.label
                                color: theme.muted
                                font.pixelSize: 9
                                font.bold: true
                                font.letterSpacing: 0.7
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.fillWidth: true
                                Layout.topMargin: 2
                                text: modelData.note
                                color: theme.faint
                                font.pixelSize: 9
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            Label {
                Layout.topMargin: 7
                Layout.leftMargin: 2
                text: "WHAT'S NEXT"
                color: theme.faint
                font.pixelSize: 11
                font.bold: true
                font.letterSpacing: 1.4
            }

            Card {
                padding: 0
                tappable: true
                onTapped: root.openTab(3)

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: mealContent.implicitHeight + 40
                    radius: theme.cardRadius
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: theme.mealStart }
                        GradientStop { position: 1.0; color: theme.mealEnd }
                    }

                    ColumnLayout {
                        id: mealContent
                        x: 20
                        y: 20
                        width: parent.width - 40
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true

                            Rectangle {
                                implicitWidth: mealKind.implicitWidth + 18
                                implicitHeight: 25
                                radius: 13
                                color: theme.accentSoft

                                Label {
                                    id: mealKind
                                    anchors.centerIn: parent
                                    text: dining.hasNextMeal
                                          ? (dining.nextMealWhen + " · " + dining.nextMealLabel).toUpperCase()
                                          : "UP NEXT"
                                    color: theme.accent
                                    font.pixelSize: 9
                                    font.bold: true
                                    font.letterSpacing: 0.8
                                }
                            }

                            Item { Layout.fillWidth: true }

                            Label {
                                visible: dining.hasNextMeal
                                text: dining.nextMealHours
                                color: theme.muted
                                font.pixelSize: 11
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.topMargin: 13
                            text: dining.nextMealVenue.length > 0
                                  ? dining.nextMealVenue : "Home Cooking"
                            color: theme.text
                            font.pixelSize: 22
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.topMargin: 10
                            visible: !dining.hasNextMeal
                            text: dining.busy ? "Loading the menu…"
                                              : "No menu has been published yet."
                            color: theme.muted
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }

                        Repeater {
                            model: dining.nextMealItems

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: 11
                                spacing: 10

                                Rectangle {
                                    width: 5
                                    height: 5
                                    radius: 3
                                    color: theme.cedar
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: model.text
                                    color: theme.text
                                    font.pixelSize: 13
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                }
            }

            Card {
                // In term the meter is the whole card, so its squircles sit
                // close to the card's own rounded corners.
                padding: semester.inTerm ? 10 : theme.cardPadding

                MeterBar {
                    visible: semester.inTerm
                    fraction: semester.elapsedFraction
                    title: semester.termName + " semester"
                    startText: Math.round(semester.elapsedFraction * 100) + "%"
                    startNote: "done"
                    endText: semester.daysLeft >= 0 ? semester.daysLeft : "—"
                    endNote: semester.daysLeft === 1 ? "day left" : "days left"
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: !semester.inTerm
                    spacing: 3

                    Label {
                        text: "Between terms"
                        color: theme.text
                        font.pixelSize: 14
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: semester.nextTermText.length > 0 ? semester.nextTermText
                                                               : "No term in session"
                        color: theme.muted
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }

            Repeater {
                model: [chapel.error, dining.error]

                Rectangle {
                    required property string modelData
                    Layout.fillWidth: true
                    visible: modelData.length > 0
                    implicitHeight: visible ? errorText.implicitHeight + 30 : 0
                    radius: 16
                    color: theme.dangerSoft

                    Label {
                        id: errorText
                        anchors.fill: parent
                        anchors.margins: 15
                        text: parent.modelData
                        color: theme.danger
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                }
            }

            Item { Layout.preferredHeight: 8 }
        }
    }
}
