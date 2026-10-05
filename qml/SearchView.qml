// Search, over a full page: one field, scope chips, then a top answer and
// results in sections — places with whether they are open, dishes with where
// and when, speakers. Empty, it offers suggestions and recent searches.
//
// Everything searched is already on the phone; see search.h.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    signal closeRequested()
    signal navigate(int tab, int section, int menuDay, string menuMeal)

    Theme { id: theme }

    color: theme.bg

    // Closed by Back, the back arrow or a result: let go of the keyboard and
    // the caret either way.
    onVisibleChanged: {
        if (!visible)
            field.release()
    }

    function begin() {
        field.text = ""
        search.setQuery("")
        field.forceActiveFocus()
    }

    // Put `text` in the field, as if typed (screenshots).
    function forceQuery(text) {
        field.text = text
    }

    function go(tab, section, menuDay, menuMeal) {
        search.commit()
        Qt.inputMethod.hide()
        root.navigate(tab, section, menuDay, menuMeal)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: root.SafeArea.margins.top + 8
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: theme.pageMargin
            spacing: 6

            IconButton {
                glyph: "back"
                flat: true
                text: "Back"
                onClicked: {
                    Qt.inputMethod.hide()
                    root.closeRequested()
                }
            }

            SearchBox {
                id: field
                Layout.fillWidth: true
                rightPadding: 44
                emphasised: true
                glyphColor: theme.cedar
                placeholder: "Search menus, places, speakers"
                onTextChanged: search.setQuery(text)
                onAccepted: {
                    search.commit()
                    Qt.inputMethod.hide()
                }
                Accessible.name: "Search CedarView"

                IconButton {
                    visible: field.text.length > 0
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    implicitWidth: 36
                    implicitHeight: 36
                    radius: 12
                    flat: true
                    glyph: "close"
                    iconSize: 16
                    iconColor: theme.muted
                    text: "Clear search"
                    onClicked: field.text = ""
                }
            }
        }

        Flow {
            visible: search.query.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: theme.pageMargin
            Layout.rightMargin: theme.pageMargin
            Layout.topMargin: 12
            spacing: 6
            Repeater {
                model: [
                    { label: "All", scope: 0, count: -1 },
                    { label: "Menu", scope: 1, count: search.menuCount },
                    { label: "Places", scope: 2, count: search.placeCount },
                    { label: "Chapel", scope: 3, count: search.chapelCount }
                ]
                Chip {
                    required property var modelData
                    visible: modelData.count !== 0
                    text: modelData.count > 0 ? modelData.label + " · " + modelData.count : modelData.label
                    interactive: true
                    implicitHeight: 34
                    leftPadding: 13
                    rightPadding: 13
                    fill: theme.surface
                    outlined: true
                    selected: search.scope === modelData.scope
                    onClicked: search.setScope(modelData.scope)
                }
            }
        }

        ScrollPage {
            Layout.fillWidth: true
            Layout.fillHeight: true
            topPadding: 14
            // Nothing to refresh here: the search reads what is loaded.
            boundsBehavior: Flickable.StopAtBounds

            // ---- Before a query: suggestions and recents
            SectionCaption {
                visible: search.query.length === 0
                text: "Try"
            }
            Flow {
                visible: search.query.length === 0
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: search.suggestions
                    Chip {
                        required property string modelData
                        text: modelData
                        interactive: true
                        implicitHeight: 36
                        leftPadding: 14
                        rightPadding: 14
                        fill: theme.surface
                        outlined: true
                        textColor: theme.text
                        onClicked: field.text = modelData
                    }
                }
            }

            SectionCaption {
                visible: search.query.length === 0 && search.recent.length > 0
                Layout.topMargin: 10
                text: "Recent"
            }
            Card {
                visible: search.query.length === 0 && search.recent.length > 0
                topPadding: 2
                bottomPadding: 2

                Repeater {
                    model: search.recent
                    Item {
                        id: recentRow
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true
                        implicitHeight: 46

                        Rectangle {
                            visible: recentRow.index > 0
                            width: parent.width
                            height: 1
                            color: theme.line
                        }
                        TapHandler { onTapped: field.text = recentRow.modelData }
                        RowLayout {
                            anchors.fill: parent
                            spacing: 12
                            Glyph {
                                Layout.preferredWidth: 17
                                Layout.preferredHeight: 17
                                kind: "clock"
                                color: theme.muted
                            }
                            Text {
                                Layout.fillWidth: true
                                text: recentRow.modelData
                                color: theme.text
                                font.family: theme.ui
                                font.pixelSize: 14
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            // ---- The top answer
            HeroCard {
                visible: search.query.length > 0 && search.hasTopAnswer
                tappable: true
                onTapped: root.go(search.topAnswer.tab, search.topAnswer.diningSection,
                                  search.topAnswer.menuDay, search.topAnswer.menuMeal)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: (search.topAnswer.kicker || "").toUpperCase()
                        color: theme.goldHero
                        font.family: theme.ui
                        font.pixelSize: 11
                        font.weight: Font.Bold
                        font.letterSpacing: 1.1
                        elide: Text.ElideRight
                    }
                    Chip {
                        visible: (search.topAnswer.tag || "").length > 0
                        text: search.topAnswer.tag || ""
                        implicitHeight: 24
                        textColor: theme.navyText
                        fill: theme.heroChip
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    text: search.topAnswer.headline || ""
                    color: theme.navyText
                    font.family: theme.display
                    font.pixelSize: 24
                    font.weight: Font.Bold
                    wrapMode: Text.Wrap
                }
                Flow {
                    visible: (search.topAnswer.chips || []).length > 0
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    spacing: 6
                    Repeater {
                        model: search.topAnswer.chips || []
                        Chip {
                            required property string modelData
                            text: modelData
                            textColor: theme.goldHero
                            fill: Qt.rgba(0.99, 0.72, 0.07, 0.16)
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 14
                    implicitHeight: 1
                    color: theme.heroLine
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    spacing: 8
                    Glyph {
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 16
                        kind: "clock"
                        color: theme.navyMuted
                    }
                    Text {
                        Layout.fillWidth: true
                        text: search.topAnswer.footer || ""
                        color: theme.navyMuted
                        font.family: theme.ui
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }
                }
            }

            // ---- Results, in sections
            Repeater {
                model: search.results

                Loader {
                    id: result
                    required property string rowType
                    required property string section
                    required property string title
                    required property string detail
                    required property string badge
                    required property bool hot
                    required property string icon
                    required property int tab
                    required property int diningSection
                    required property int menuDay
                    required property string menuMeal

                    Layout.fillWidth: true
                    sourceComponent: rowType === "section" ? sectionHeader : resultCard

                    Component {
                        id: sectionHeader
                        SectionCaption { text: result.title }
                    }
                    Component {
                        id: resultCard
                        Card {
                            padding: 14
                            tappable: true
                            onTapped: root.go(result.tab, result.diningSection, result.menuDay, result.menuMeal)

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                Rectangle {
                                    Layout.preferredWidth: 42
                                    Layout.preferredHeight: 42
                                    radius: 14
                                    color: result.icon === "chapel" ? theme.cedarSoft
                                         : result.icon === "campus" ? theme.surface2 : theme.goldSoft
                                    Glyph {
                                        anchors.centerIn: parent
                                        width: 18
                                        height: 18
                                        kind: result.icon
                                        color: result.icon === "chapel" ? theme.cedar
                                             : result.icon === "campus" ? theme.text : theme.gold
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 0
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: result.title
                                        color: theme.text
                                        font.family: theme.ui
                                        font.pixelSize: 15
                                        font.weight: Font.Bold
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: text.length > 0
                                        text: result.detail
                                        color: theme.muted
                                        font.family: theme.ui
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                    }
                                }
                                Chip {
                                    visible: result.badge.length > 0
                                    text: result.badge
                                    textColor: result.hot ? theme.gold : theme.muted
                                    fill: result.hot ? theme.goldSoft : theme.surface2
                                }
                            }
                        }
                    }
                }
            }

            Text {
                visible: search.query.length > 0 && !search.hasResults
                Layout.fillWidth: true
                Layout.topMargin: 24
                horizontalAlignment: Text.AlignHCenter
                text: "Nothing matches “" + search.query + "”."
                color: theme.muted
                font.family: theme.ui
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }
        }
    }
}
