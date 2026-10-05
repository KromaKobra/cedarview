// Dining › Plan: meals and flex left, how fast the flex is going against what
// lasts the term, meal exchanges, and the card's recent activity.

import QtQuick
import QtQuick.Layouts

ScrollPage {
    id: root

    Theme { id: theme }

    readonly property var plan: dining.planStatus

    HeroCard {
        padding: 20

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: (root.plan.hasData ? dining.planTitle : "Meal plan").toUpperCase()
                color: theme.goldHero
                font.family: theme.ui
                font.pixelSize: 11
                font.weight: Font.Bold
                font.letterSpacing: 1.1
                elide: Text.ElideRight
            }
            StatusStamp {
                status: root.plan
                navy: true
                verbose: true
            }
            Text {
                visible: !root.plan.showStamp
                text: chapel.termLabel
                color: theme.navyMuted
                font.family: theme.ui
                font.pixelSize: 12
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 10
            spacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Skeleton {
                    visible: !root.plan.hasData && root.plan.loading
                    navy: true
                    Layout.preferredWidth: 110
                    Layout.preferredHeight: 56
                    radius: 14
                }
                Row {
                    visible: root.plan.hasData
                    Text {
                        id: mealsFigure
                        text: dining.mealsRemaining >= 0 ? dining.mealsRemaining : "—"
                        color: theme.goldHero
                        font.family: theme.display
                        font.pixelSize: 68
                        font.weight: Font.ExtraBold
                        font.letterSpacing: -3
                    }
                    Text {
                        anchors.baseline: mealsFigure.baseline
                        visible: dining.mealsPerPeriod > 0 && dining.mealsRemaining >= 0
                        text: " / " + dining.mealsPerPeriod
                        color: theme.navyMuted
                        font.family: theme.display
                        font.pixelSize: 24
                        font.weight: Font.DemiBold
                    }
                }
                Text {
                    visible: root.plan.hasData
                    text: "meals " + dining.mealsPeriodText
                    color: theme.navyMuted
                    font.family: theme.ui
                    font.pixelSize: 13
                }
            }

            ColumnLayout {
                Layout.alignment: Qt.AlignBottom
                spacing: 6
                Skeleton {
                    visible: !root.plan.hasData && root.plan.loading
                    navy: true
                    Layout.alignment: Qt.AlignRight
                    Layout.preferredWidth: 96
                    Layout.preferredHeight: 28
                }
                Text {
                    visible: root.plan.hasData
                    Layout.alignment: Qt.AlignRight
                    text: dining.diningDollars.length > 0 ? dining.diningDollars : "—"
                    color: theme.navyText
                    font.family: theme.display
                    font.pixelSize: 32
                    font.weight: Font.Bold
                }
                Text {
                    visible: root.plan.hasData
                    Layout.alignment: Qt.AlignRight
                    text: "flex dollars"
                    color: theme.navyMuted
                    font.family: theme.ui
                    font.pixelSize: 13
                }
            }
        }

        PipMeter {
            visible: root.plan.hasData && dining.mealsPerPeriod > 0 && dining.mealsRemaining >= 0
            Layout.fillWidth: true
            Layout.topMargin: 14
            total: dining.mealsPerPeriod
            filled: dining.mealsRemaining
            pipHeight: 10
            colorA: theme.heroPipA
            colorB: theme.heroPipB
            offColor: Qt.rgba(1, 1, 1, 0.15)
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.topMargin: 14
            status: root.plan
            what: "your meal plan"
            navy: true
        }

        // ---- Flex pace
        Rectangle {
            visible: root.plan.hasData && dining.hasPace
            Layout.fillWidth: true
            Layout.topMargin: 16
            implicitHeight: pace.implicitHeight + 28
            radius: 18
            color: theme.heroInset

            ColumnLayout {
                id: pace
                x: 14
                y: 14
                width: parent.width - 28
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: "Flex pace"
                        color: theme.navyText
                        font.family: theme.ui
                        font.pixelSize: 14
                        font.weight: Font.Bold
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        text: dining.flexPerWeekText + " lasts to " + dining.paceEndText
                        color: theme.navyMuted
                        font.family: theme.ui
                        font.pixelSize: 13
                        elide: Text.ElideLeft
                    }
                }

                // Spent this week against the weekly figure: solid up to
                // whichever is less, striped beyond the figure when over it,
                // and a marker where the figure is.
                Item {
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    implicitHeight: 10

                    Rectangle {
                        anchors.fill: parent
                        radius: 5
                        color: Qt.rgba(1, 1, 1, 0.12)
                    }
                    Rectangle {
                        width: parent.width * Math.min(dining.paceSpentFraction, dining.paceBudgetFraction)
                        height: parent.height
                        radius: 5
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: theme.paceA }
                            GradientStop { position: 1.0; color: theme.paceB }
                        }
                    }
                    // The overspend, striped.
                    Item {
                        visible: dining.overPace
                        x: parent.width * dining.paceBudgetFraction
                        width: parent.width * (dining.paceSpentFraction - dining.paceBudgetFraction)
                        height: parent.height
                        clip: true
                        Row {
                            spacing: 3
                            Repeater {
                                model: Math.ceil(parent.parent.width / 6) + 2
                                Rectangle {
                                    width: 3
                                    height: 20
                                    y: -5
                                    rotation: 35
                                    color: theme.paceA
                                }
                            }
                        }
                    }
                    Rectangle {
                        x: parent.width * dining.paceBudgetFraction - 1
                        y: -4
                        width: 2
                        height: parent.height + 8
                        radius: 1
                        color: theme.navyText
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    Text {
                        text: "Spent this week <b>" + dining.spentThisWeek + "</b>"
                        textFormat: Text.StyledText
                        color: theme.navyMuted
                        font.family: theme.ui
                        font.pixelSize: 13
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: dining.paceDeltaText
                        color: dining.overPace ? theme.heroDanger : theme.heroSky
                        font.family: theme.ui
                        font.pixelSize: 13
                        font.weight: Font.Bold
                    }
                }
            }
        }

        // ---- Exchanges, and any flex that does not expire
        RowLayout {
            visible: root.plan.hasData && dining.mealExchanges >= 0
            Layout.fillWidth: true
            Layout.topMargin: 14
            spacing: 10
            Glyph {
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                kind: "swap"
                color: theme.goldHero
            }
            Text {
                Layout.fillWidth: true
                text: "<b>" + dining.mealExchanges + (dining.mealExchanges === 1 ? " meal exchange" : " meal exchanges")
                      + "</b>" + (dining.exchangeText.length > 0 ? " · " + dining.exchangeText : "")
                textFormat: Text.StyledText
                color: theme.navyMuted
                font.family: theme.ui
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
        }
        RowLayout {
            visible: root.plan.hasData && dining.hasFlexDollars
            Layout.fillWidth: true
            Layout.topMargin: 10
            spacing: 10
            Glyph {
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                kind: "card"
                color: theme.paceA
            }
            Text {
                Layout.fillWidth: true
                text: "<b>" + dining.flexDollars + "</b> voluntary flex · doesn't expire"
                textFormat: Text.StyledText
                color: theme.navyMuted
                font.family: theme.ui
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
        }
    }

    // ---- Activity --------------------------------------------------------------
    RowLayout {
        visible: root.plan.hasData
        Layout.fillWidth: true
        Layout.topMargin: 8
        Layout.leftMargin: 4
        spacing: 6

        Text {
            Layout.fillWidth: true
            text: "ACTIVITY"
            color: theme.faint
            font.family: theme.ui
            font.pixelSize: 11
            font.weight: Font.Bold
            font.letterSpacing: 1.1
        }
        Repeater {
            model: ["All", "Meals", "Flex"]
            Chip {
                required property string modelData
                required property int index
                text: modelData
                interactive: true
                implicitHeight: 34
                leftPadding: 14
                rightPadding: 14
                fill: theme.surface
                outlined: true
                selected: dining.activityFilter === index
                onClicked: dining.setActivityFilter(index)
            }
        }
    }

    Text {
        visible: root.plan.hasData && text.length > 0
        Layout.fillWidth: true
        Layout.leftMargin: 4
        text: dining.activitySummary
        color: theme.muted
        font.family: theme.ui
        font.pixelSize: 13
        elide: Text.ElideRight
    }

    Card {
        visible: root.plan.hasData
        topPadding: 4
        bottomPadding: 8

        Repeater {
            id: activityRows
            model: dining.activity

            Item {
                id: activity
                required property int index
                required property bool isHeader
                required property string title
                required property string detail
                required property string amount
                required property string kind

                Layout.fillWidth: true
                implicitHeight: isHeader ? dayHeader.implicitHeight + (index > 0 ? 22 : 16)
                                         : activityRow.implicitHeight + 18

                Rectangle {
                    visible: activity.isHeader && activity.index > 0
                    y: 4
                    width: parent.width
                    height: 1
                    color: theme.line
                }

                RowLayout {
                    id: dayHeader
                    visible: activity.isHeader
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    Text {
                        Layout.fillWidth: true
                        text: activity.title
                        color: theme.cedar
                        font.family: theme.ui
                        font.pixelSize: 13
                        font.weight: Font.Bold
                    }
                    Text {
                        text: activity.detail
                        color: theme.faint
                        font.family: theme.ui
                        font.pixelSize: 12
                    }
                }

                RowLayout {
                    id: activityRow
                    visible: !activity.isHeader
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12

                    Rectangle {
                        Layout.preferredWidth: 38
                        Layout.preferredHeight: 38
                        radius: 13
                        color: activity.kind === "flex" || activity.kind === "deposit" ? theme.violetSoft
                                                                                       : theme.goldSoft
                        Glyph {
                            anchors.centerIn: parent
                            width: 18
                            height: 18
                            kind: activity.kind === "exchange" ? "swap"
                                : activity.kind === "flex" || activity.kind === "deposit" ? "card" : "plate"
                            color: activity.kind === "flex" || activity.kind === "deposit" ? theme.violet
                                                                                           : theme.gold
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        spacing: 1
                        Text {
                            Layout.fillWidth: true
                            text: activity.title
                            color: theme.text
                            font.family: theme.ui
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: activity.detail
                            color: theme.muted
                            font.family: theme.ui
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }
                    Text {
                        text: activity.amount.length > 0 ? activity.amount : "1 meal"
                        color: activity.amount.length > 0 ? theme.violet : theme.muted
                        font.family: activity.amount.length > 0 ? theme.display : theme.ui
                        font.pixelSize: activity.amount.length > 0 ? 15 : 13
                        font.weight: activity.amount.length > 0 ? Font.Bold : Font.DemiBold
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 12
            Layout.bottomMargin: 6
            visible: activityRows.count === 0
            text: dining.activityEmptyText
            color: theme.muted
            font.family: theme.ui
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
    }
}
