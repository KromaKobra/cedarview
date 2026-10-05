// Today: what matters now, then the rest of the day.
//
// The hero follows the clock (today.heroKind): the chapel on a chapel
// morning, the next sitting at The Commons through the day, and curfew in the
// evening — when the rest of the screen turns to what is still open and
// tomorrow morning. Every figure is a skeleton until its source has data,
// and says how old it is when it is a saved copy.

import QtQuick
import QtQuick.Layouts

ScrollPage {
    id: root

    // Where a tap goes; see Main.navigateTo().
    signal navigate(int tab, int section, int menuDay, string menuMeal)
    // A chapel's details, for the sheet.
    signal showChapel(var details)

    Theme { id: theme }

    readonly property var skips: chapel.skipsStatus
    readonly property var plan: dining.planStatus
    readonly property var menu: dining.menuStatus
    readonly property bool night: today.heroKind === "curfew"

    // ---- The chapel hero ---------------------------------------------------
    HeroCard {
        visible: today.heroKind === "chapel"
        tappable: true
        onTapped: root.showChapel({
            who: today.chapelSpeaker, subtitle: "", description: today.chapelDescription,
            dateText: today.chapelDateText, timeText: today.chapelTime, startsAt: today.chapelStartsAt,
            livestream: today.chapelLivestream, youtubeId: today.chapelYoutubeId, isToday: true
        })

        RowLayout {
            Layout.fillWidth: true
            spacing: 9

            LiveDot {}
            Text {
                Layout.fillWidth: true
                text: ("Chapel · " + today.chapelCountdown).toUpperCase()
                color: theme.goldHero
                font.family: theme.ui
                font.pixelSize: 11
                font.weight: Font.Bold
                font.letterSpacing: 1.1
                elide: Text.ElideRight
            }
            ClockText {
                text: today.chapelTime
                color: theme.navyText
                meridiemColor: theme.navyMuted
                size: 16
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 12
            text: today.chapelSpeaker
            color: theme.navyText
            font.family: theme.display
            font.pixelSize: 29
            font.weight: Font.Bold
            lineHeight: 1.05
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            Layout.topMargin: 5
            visible: text.length > 0
            text: today.chapelDescription
            color: theme.navyMuted
            font.family: theme.ui
            font.pixelSize: 13
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        SkipsInset {
            Layout.fillWidth: true
            Layout.topMargin: 14
        }
    }

    // ---- The meal hero -------------------------------------------------------
    HeroCard {
        visible: today.heroKind === "meal"
        tappable: true
        onTapped: root.navigate(2, 1, 0, today.mealSlot)

        RowLayout {
            Layout.fillWidth: true
            spacing: 9

            LiveDot {
                visible: today.mealLive
            }
            Text {
                Layout.fillWidth: true
                text: (today.mealLabel + " · " + today.mealStatus).toUpperCase()
                color: theme.goldHero
                font.family: theme.ui
                font.pixelSize: 11
                font.weight: Font.Bold
                font.letterSpacing: 1.1
                elide: Text.ElideRight
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 12
            text: "Home Cooking"
            color: theme.navyText
            font.family: theme.display
            font.pixelSize: 29
            font.weight: Font.Bold
        }
        Text {
            Layout.fillWidth: true
            Layout.topMargin: 4
            text: today.mealHoursText
            color: theme.navyMuted
            font.family: theme.ui
            font.pixelSize: 13
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 14
            implicitHeight: mealInset.implicitHeight + 26
            radius: 18
            color: theme.heroInset

            ColumnLayout {
                id: mealInset
                x: 14
                y: 13
                width: parent.width - 28
                spacing: 8

                Repeater {
                    model: today.mealItems

                    Text {
                        required property string modelData
                        Layout.fillWidth: true
                        text: modelData
                        color: theme.navyText
                        font.family: theme.ui
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                }
                Repeater {
                    model: today.mealItems.length === 0 && root.menu.loading ? 3 : 0
                    Skeleton {
                        required property int index
                        navy: true
                        Layout.preferredWidth: [180, 140, 160][index]
                        Layout.preferredHeight: 12
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: today.mealItems.length === 0 && !root.menu.loading
                    text: root.menu.hasData ? "The menu for this sitting isn't posted."
                                            : "The menu hasn't loaded."
                    color: theme.navyMuted
                    font.family: theme.ui
                    font.pixelSize: 13
                }
                RowLayout {
                    visible: today.mealExtra.length > 0
                    Layout.fillWidth: true
                    spacing: 6
                    Glyph {
                        Layout.preferredWidth: 15
                        Layout.preferredHeight: 15
                        kind: "leaf"
                        color: theme.heroSky
                    }
                    Text {
                        Layout.fillWidth: true
                        text: today.mealExtra
                        color: theme.navyMuted
                        font.family: theme.ui
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // ---- The curfew hero -------------------------------------------------------
    CurfewHero {
        visible: root.night
        tappable: true
        onTapped: root.navigate(3, -1, 0, "")
        showRules: false
    }

    // ---- Meals and flex ----------------------------------------------------
    GridLayout {
        visible: !root.night
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 10
        rowSpacing: 10

        Card {
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            padding: 14
            tappable: true
            onTapped: root.navigate(2, 0, 0, "")

            RowLayout {
                Layout.fillWidth: true
                spacing: 7
                Glyph {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    kind: "plate"
                    color: theme.gold
                }
                Caps { Layout.fillWidth: true; text: "Meals" }
                StatusStamp { status: root.plan }
            }

            Skeleton {
                visible: !root.plan.hasData && root.plan.loading
                Layout.topMargin: 12
                Layout.preferredWidth: 76
                Layout.preferredHeight: 30
            }
            Row {
                visible: root.plan.hasData
                Layout.topMargin: 10
                Text {
                    text: dining.mealsRemaining >= 0 ? dining.mealsRemaining : "—"
                    color: theme.text
                    font.family: theme.display
                    font.pixelSize: 34
                    font.weight: Font.Bold
                }
                Text {
                    anchors.baseline: parent.children[0].baseline
                    visible: dining.mealsPerPeriod > 0 && dining.mealsRemaining >= 0
                    text: " / " + dining.mealsPerPeriod
                    color: theme.faint
                    font.family: theme.display
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                }
            }
            Text {
                visible: root.plan.hasData
                Layout.topMargin: 5
                text: dining.mealsPeriodText
                color: theme.muted
                font.family: theme.ui
                font.pixelSize: 12
            }
            PipMeter {
                visible: root.plan.hasData && dining.mealsPerPeriod > 0 && dining.mealsRemaining >= 0
                Layout.fillWidth: true
                Layout.topMargin: 10
                total: dining.mealsPerPeriod
                filled: dining.mealsRemaining
                pipHeight: 6
                gap: 2
            }
            EmptyState {
                Layout.fillWidth: true
                Layout.topMargin: 10
                status: root.plan
                what: "your meals"
            }
        }

        Card {
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            padding: 14
            tappable: true
            onTapped: root.navigate(2, 0, 0, "")

            RowLayout {
                Layout.fillWidth: true
                spacing: 7
                Glyph {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    kind: "card"
                    color: theme.violet
                }
                Caps { Layout.fillWidth: true; text: "Flex" }
                StatusStamp { status: root.plan }
            }

            Skeleton {
                visible: !root.plan.hasData && root.plan.loading
                Layout.topMargin: 12
                Layout.preferredWidth: 100
                Layout.preferredHeight: 30
            }
            Text {
                visible: root.plan.hasData
                Layout.fillWidth: true
                Layout.topMargin: 12
                text: dining.diningDollars.length > 0 ? dining.diningDollars : "—"
                color: theme.text
                font.family: theme.display
                font.pixelSize: 30
                font.weight: Font.Bold
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 20
            }
            Text {
                visible: root.plan.hasData && dining.hasPace
                Layout.fillWidth: true
                Layout.topMargin: 6
                text: "About <b><font color=\"" + theme.text + "\">" + dining.flexPerWeekText
                      + "</font></b> lasts to " + dining.paceEndText
                textFormat: Text.StyledText
                color: theme.muted
                font.family: theme.ui
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
            EmptyState {
                Layout.fillWidth: true
                Layout.topMargin: 10
                status: root.plan
                what: "your flex"
            }
        }
    }

    // ---- The rest of today -------------------------------------------------
    SectionCaption {
        visible: !root.night && today.hasAgenda
        text: "Rest of today"
        note: today.nowText
    }

    Card {
        visible: !root.night && today.hasAgenda
        topPadding: 14
        bottomPadding: 4

        Repeater {
            model: today.agenda

            TimelineRow {
                time: model.time
                meridiem: model.meridiem
                title: model.title
                chip: model.chip
                detail: model.detail
                extra: model.extra
                accent: model.accent
                last: model.isLast
                onTapped: root.navigate(model.tab, model.diningSection, 0, model.menuMeal)
            }
        }
    }

    // ---- Night: still open, and tomorrow morning ----------------------------
    SectionCaption {
        visible: root.night && today.stillOpen.length > 0
        text: "Still open"
        note: "Closing first at top"
    }

    Card {
        visible: root.night && today.stillOpen.length > 0
        topPadding: 4
        bottomPadding: 4

        Repeater {
            model: today.stillOpen

            PlaceRow {
                required property var modelData
                required property int index
                name: modelData.name
                detail: modelData.detail
                badge: modelData.closesIn
                hot: modelData.soon
                icon: modelData.kind === "dining" ? "dining" : "campus"
                iconHot: modelData.kind === "dining"
                divider: index > 0
                onTapped: root.navigate(modelData.kind === "dining" ? 2 : 3, modelData.kind === "dining" ? 2 : -1, 0, "")
            }
        }
    }

    SectionCaption {
        visible: root.night && today.tomorrow.length > 0
        text: today.morningTitle
        note: today.tomorrowDateText
    }

    Card {
        visible: root.night && today.tomorrow.length > 0
        topPadding: 4
        bottomPadding: 4

        Repeater {
            model: today.tomorrow

            Item {
                id: morningRow
                required property var modelData
                required property int index
                Layout.fillWidth: true
                implicitHeight: morningLayout.implicitHeight + 24

                Rectangle {
                    visible: morningRow.index > 0
                    width: parent.width
                    height: 1
                    color: theme.line
                }

                TapHandler {
                    onTapped: root.navigate(morningRow.modelData.tab, morningRow.modelData.diningSection,
                                            morningRow.modelData.menuDay, morningRow.modelData.menuMeal)
                }

                RowLayout {
                    id: morningLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12

                    ColumnLayout {
                        Layout.preferredWidth: 50
                        Layout.alignment: Qt.AlignTop
                        spacing: 0
                        Text {
                            text: morningRow.modelData.time
                            color: theme.text
                            font.family: theme.display
                            font.pixelSize: 17
                            font.weight: Font.Bold
                        }
                        Text {
                            text: morningRow.modelData.meridiem
                            color: theme.faint
                            font.family: theme.ui
                            font.pixelSize: 11
                            font.weight: Font.Bold
                            font.letterSpacing: 0.8
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            text: morningRow.modelData.title
                            color: theme.text
                            font.family: theme.ui
                            font.pixelSize: 15
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: morningRow.modelData.detail
                            color: theme.muted
                            font.family: theme.ui
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: morningRow.modelData.kind === "chapel" && chapel.remaining >= 0
                            text: chapel.remaining + (chapel.remaining === 1 ? " skip left" : " skips left")
                            color: theme.gold
                            font.family: theme.ui
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }
    }

    // ---- Small pieces -------------------------------------------------------

    // A card's small capitals.
    component Caps: Text {
        color: theme.faint
        font.family: theme.ui
        font.pixelSize: 11
        font.weight: Font.Bold
        font.letterSpacing: 1.1
        font.capitalization: Font.AllUppercase
        elide: Text.ElideRight
    }

    // The skips, inside the chapel hero: the figure, standing, and pips.
    component SkipsInset: Rectangle {
        implicitHeight: inset.implicitHeight + 26
        radius: 18
        color: theme.heroInset

        ColumnLayout {
            id: inset
            x: 14
            y: 12
            width: parent.width - 28
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 7

                Skeleton {
                    visible: !root.skips.hasData && root.skips.loading
                    navy: true
                    Layout.preferredWidth: 130
                    Layout.preferredHeight: 22
                }
                Text {
                    visible: root.skips.hasData
                    text: chapel.remaining >= 0 ? chapel.remaining : "—"
                    color: theme.goldHero
                    font.family: theme.display
                    font.pixelSize: 26
                    font.weight: Font.ExtraBold
                }
                Text {
                    visible: root.skips.hasData
                    text: chapel.allowed >= 0 ? "of " + chapel.allowed + " skips left" : "skips left"
                    color: theme.navyMuted
                    font.family: theme.ui
                    font.pixelSize: 13
                }
                Item { Layout.fillWidth: true }
                StatusStamp {
                    status: root.skips
                    navy: true
                    verbose: true
                }
                RowLayout {
                    visible: root.skips.hasData && !root.skips.showStamp
                    spacing: 5
                    Glyph {
                        Layout.preferredWidth: 15
                        Layout.preferredHeight: 15
                        kind: chapel.inGoodStanding ? "checkCircle" : "info"
                        color: chapel.inGoodStanding ? theme.heroSky : theme.heroDanger
                    }
                    Text {
                        text: chapel.inGoodStanding ? "Good standing" : "Needs attention"
                        color: chapel.inGoodStanding ? theme.heroSky : theme.heroDanger
                        font.family: theme.ui
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                }
            }

            PipMeter {
                visible: root.skips.hasData && chapel.allowed > 0 && chapel.remaining >= 0
                Layout.fillWidth: true
                total: chapel.allowed
                filled: chapel.remaining
                pipHeight: 7
                colorA: theme.heroPipA
                colorB: theme.heroPipB
                offColor: theme.heroPipOff
            }

            EmptyState {
                Layout.fillWidth: true
                status: root.skips
                what: "your skips"
                navy: true
            }
        }
    }
}
