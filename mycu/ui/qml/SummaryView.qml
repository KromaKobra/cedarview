// The summary screen: the things worth knowing before you leave the room.
//
//   1. what the next chapel is, and how many skips you still have
//   2. the two flex balances, which are NOT interchangeable
//   3. how many meals are left on the plan
//   4. what Home Cooking is serving at the next sitting
//   5. how much of the semester is left
//
// Binds to the `chapel`, `dining` and `semester` context properties. Both
// screens' viewmodels feed this one, which is the point of it: the two things a
// student checks in the morning live on two different services and used to live
// on two different tabs.
//
// Nothing here renders a confident zero. Every figure the app has not actually
// received is an em dash, because "$0.00" and "0 skips left" are the two most
// alarming things this app could say, and it must never say either by accident.

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

        // Pull to refresh. Works with a mouse on desktop too, which is what
        // makes it testable without a phone.
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

            // ---- Chapel -------------------------------------------------
            Card {
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: "NEXT CHAPEL"
                            color: theme.muted
                            font.pixelSize: 11
                            font.bold: true
                            font.letterSpacing: 1.2
                        }

                        Item { Layout.fillWidth: true }

                        // "TOMORROW". The day is the part you act on, so it is
                        // the part that gets the pill.
                        Rectangle {
                            visible: chapel.nextChapelDay.length > 0
                            radius: height / 2
                            color: theme.violetSoft
                            implicitWidth: dayLabel.implicitWidth + 20
                            implicitHeight: 22

                            Label {
                                id: dayLabel
                                anchors.centerIn: parent
                                text: chapel.nextChapelDay.toUpperCase()
                                color: theme.violet
                                font.pixelSize: 10
                                font.bold: true
                                font.letterSpacing: 1.0
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.topMargin: 10
                        text: chapel.nextSpeaker.length > 0
                              ? chapel.nextSpeaker
                              : "No chapel scheduled"
                        color: chapel.nextSpeaker.length > 0 ? theme.text : theme.muted
                        font.pixelSize: 30
                        font.bold: true
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        visible: text.length > 0
                        // The title is appended only when it says something the
                        // headline did not — the feed sets Title to the
                        // speaker's name about half the time.
                        text: chapel.nextChapelDateText
                              + (chapel.nextChapelTitle.length > 0
                                 ? " · " + chapel.nextChapelTitle : "")
                        color: theme.muted
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 18
                        Layout.bottomMargin: 16
                        height: 1
                        color: theme.divider
                    }

                    Label {
                        text: "Chapel skips remaining"
                        color: theme.muted
                        font.pixelSize: 12
                    }

                    // Unknown until a fetch lands, and shown as unknown. An
                    // unloaded screen that reads "0 of 0" is the app lying in
                    // the reassuring direction, which is the worse direction.
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        spacing: 8

                        Label {
                            text: chapel.remaining >= 0 ? chapel.remaining : "—"
                            color: theme.text
                            font.pixelSize: 32
                            font.bold: true
                        }

                        Label {
                            Layout.alignment: Qt.AlignBottom
                            Layout.bottomMargin: 5
                            visible: chapel.allowed >= 0
                            text: "/ " + chapel.allowed + " this semester"
                            color: theme.muted
                            font.pixelSize: 12
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            Layout.alignment: Qt.AlignBottom
                            Layout.bottomMargin: 5
                            visible: !chapel.inGoodStanding && chapel.loaded
                            text: "Not in good standing"
                            color: theme.danger
                            font.pixelSize: 11
                            font.bold: true
                        }
                    }

                    // Hidden rather than empty until a fetch lands — see
                    // MeterBar.qml for why it is unlabelled. This one is the
                    // remaining share: it empties as skips are spent.
                    MeterBar {
                        Layout.topMargin: 14
                        visible: chapel.remaining >= 0 && chapel.allowed > 0
                        fraction: chapel.remainingFraction
                    }
                }
            }

            // ---- Meal plan ----------------------------------------------
            Label {
                Layout.topMargin: 10
                Layout.leftMargin: 4
                text: "MEAL PLAN"
                color: theme.faint
                font.pixelSize: 11
                font.bold: true
                font.letterSpacing: 1.2
            }

            // Two balances, side by side and never added together. The plan's
            // own Flex Dollars expire at the end of the term; purchased
            // Voluntary Flex Dollars do not. Each keeps its expiry on screen
            // underneath it, because that is the whole difference between them.
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.gap

                BalanceTile {
                    caption: "Temporary Flex"
                    amount: dining.diningDollars
                    footnote: "Expires this semester"
                    dotColor: theme.violet
                }

                BalanceTile {
                    caption: "Permanent Flex"
                    amount: dining.flexDollars
                    footnote: "Rolls over"
                    dotColor: theme.accent
                }
            }

            Card {
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            text: "Your plan"
                            color: theme.muted
                            font.pixelSize: 11
                        }

                        Label {
                            Layout.fillWidth: true
                            // The plan's name from Self-Service ("21 Meals per
                            // week"), or just its cycle when no name came back.
                            text: dining.planDescription.length > 0
                                  ? dining.planDescription
                                  : "Meal plan"
                            color: theme.text
                            font.pixelSize: 18
                            font.bold: true
                            elide: Text.ElideRight
                        }
                    }

                    ColumnLayout {
                        spacing: 2

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: dining.mealsRemaining >= 0 ? dining.mealsRemaining : "—"
                            color: theme.accent
                            font.pixelSize: 28
                            font.bold: true
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: dining.mealsPeriodText
                            color: theme.faint
                            font.pixelSize: 10
                        }
                    }
                }
            }

            // ---- The next sitting ---------------------------------------
            Card {
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: dining.hasNextMeal
                                  ? (dining.nextMealWhen + " · " + dining.nextMealLabel).toUpperCase()
                                  : "UP NEXT"
                            color: theme.accent
                            font.pixelSize: 11
                            font.bold: true
                            font.letterSpacing: 1.1
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        text: dining.nextMealVenue
                        color: theme.text
                        font.pixelSize: 20
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.topMargin: 10
                        visible: !dining.hasNextMeal
                        text: dining.busy
                              ? "Loading the menu…"
                              : "No menu published for the next sitting yet."
                        color: theme.muted
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        model: dining.nextMealItems

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 14
                            spacing: 12

                            Rectangle {
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 6
                                width: 5
                                height: 5
                                radius: 2.5
                                color: theme.faint
                            }

                            // Dish names only. The allergen list is still on the
                            // model (`allergens`, which the Chucks tab shows)
                            // but it is not what this card is for: this
                            // is the glance that tells you whether to walk over
                            // to The Commons, and a grey second line under every
                            // item turned four dishes into eight lines of text.
                            Label {
                                Layout.fillWidth: true
                                text: model.text
                                color: theme.text
                                font.pixelSize: 14
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }

            // ---- The term itself ----------------------------------------
            // The only card on this screen with no service behind it — the
            // term's dates are hand-entered, for the reason core/calendar.py
            // explains. Last of the real cards because it is the slowest-moving
            // number on the screen: it is here to be seen, not checked.
            Card {
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: "SEMESTER"
                            color: theme.muted
                            font.pixelSize: 11
                            font.bold: true
                            font.letterSpacing: 1.2
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            Layout.alignment: Qt.AlignVCenter
                            text: semester.inTerm
                                  ? semester.termName + " · " + semester.endDateText
                                  : "Between terms"
                            color: theme.faint
                            font.pixelSize: 11
                        }
                    }

                    // In term: the countdown. Out of term: when the next one
                    // starts. Never "0 days left" over the summer — the same
                    // rule the rest of this screen follows.
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 10
                        visible: semester.inTerm
                        spacing: 8

                        Label {
                            text: semester.daysLeft >= 0 ? semester.daysLeft : "—"
                            color: theme.text
                            font.pixelSize: 32
                            font.bold: true
                        }

                        Label {
                            Layout.alignment: Qt.AlignBottom
                            Layout.bottomMargin: 5
                            text: (semester.totalDays >= 0 ? "/ " + semester.totalDays + " " : "")
                                  + (semester.daysLeft === 1 ? "day left" : "days left")
                            color: theme.muted
                            font.pixelSize: 12
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.topMargin: 10
                        visible: !semester.inTerm
                        text: semester.nextTermText.length > 0
                              ? semester.nextTermText
                              : "No term in session."
                        color: theme.muted
                        font.pixelSize: 16
                        wrapMode: Text.Wrap
                    }

                    // Fills as the term runs — completed, not remaining, unlike
                    // the skip bar above.
                    MeterBar {
                        Layout.topMargin: 14
                        visible: semester.inTerm
                        fraction: semester.elapsedFraction
                    }
                }
            }

            // ---- Anything that went wrong -------------------------------
            // Last, not first: a failed chapel fetch should not push the menu
            // off the screen, and by the time you read this the cards above
            // are already showing what did load.
            Repeater {
                model: [chapel.error, dining.error]

                Rectangle {
                    required property string modelData

                    Layout.fillWidth: true
                    visible: modelData.length > 0
                    implicitHeight: visible ? errorText.implicitHeight + 28 : 0
                    radius: 12
                    color: theme.dangerSoft
                    border.width: 1
                    border.color: theme.hairline

                    Label {
                        id: errorText
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 14
                        text: parent.modelData
                        color: theme.danger
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                }
            }

            Item { Layout.preferredHeight: 4 }
        }
    }
}
