// The Campus tab: tonight's curfew, then every building — the ones you
// starred first, then the rest grouped by when they close, soonest first,
// with the closed ones last. Search and three filters narrow it.
//
// Hours are hand-entered in src/core/hours.cpp; the `campus` viewmodel asks
// them about the clock.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollPage {
    id: root

    Theme { id: theme }

    // The groups opened by a tap, by title; the first is always open.
    property var expanded: ({})

    CurfewHero {}

    // ---- Search and filters -----------------------------------------------------
    TextField {
        id: buildingSearch
        Layout.fillWidth: true
        implicitHeight: 48
        leftPadding: 42
        rightPadding: 16
        placeholderText: campus.searchPlaceholder
        placeholderTextColor: theme.faint
        color: theme.text
        font.family: theme.ui
        font.pixelSize: 15
        inputMethodHints: Qt.ImhNoPredictiveText
        onTextChanged: campus.setQuery(text)
        Accessible.name: "Search buildings"

        background: Rectangle {
            radius: 16
            color: theme.surface
            border.width: 1
            border.color: buildingSearch.activeFocus ? theme.cedar : theme.line
        }

        Glyph {
            x: 15
            anchors.verticalCenter: parent.verticalCenter
            width: 18
            height: 18
            kind: "search"
            color: theme.muted
        }
    }

    Flow {
        Layout.fillWidth: true
        spacing: 6

        Repeater {
            model: [
                { label: "Open now · " + campus.openCount, filter: 1 },
                { label: "Closing soon · " + campus.closingSoonCount, filter: 2 },
                { label: "Closed · " + campus.closedCount, filter: 3 }
            ]
            Chip {
                required property var modelData
                text: modelData.label
                interactive: true
                implicitHeight: 34
                leftPadding: 13
                rightPadding: 13
                fill: theme.surface
                outlined: true
                selected: campus.filter === modelData.filter
                onClicked: campus.setFilter(modelData.filter)
            }
        }
    }

    // ---- Your places ---------------------------------------------------------------
    SectionCaption {
        visible: campus.favoriteBuildings.length > 0
        icon: "star"
        text: "Your places"
    }

    Card {
        visible: campus.favoriteBuildings.length > 0
        topPadding: 4
        bottomPadding: 4

        Repeater {
            model: campus.favoriteBuildings

            PlaceRow {
                required property var modelData
                required property int index
                name: modelData.name
                code: modelData.code.length > 0 ? modelData.code : modelData.name.substring(0, 2).toUpperCase()
                detail: modelData.closesText
                badge: modelData.closesIn
                hot: modelData.closingSoon
                iconHot: modelData.closingSoon
                divider: index > 0
            }
        }
    }

    // ---- By closing time ------------------------------------------------------------
    Repeater {
        model: campus.groups

        Card {
            id: group
            required property var modelData
            required property int index

            readonly property bool open: index === 0 || root.expanded[modelData.title] === true
            topPadding: 4
            bottomPadding: open ? 6 : 14

            AbstractButton {
                id: groupHeader
                Layout.fillWidth: true
                implicitHeight: 48
                focusPolicy: Qt.NoFocus
                enabled: group.index > 0
                onClicked: {
                    const next = Object.assign({}, root.expanded)
                    next[group.modelData.title] = !group.open
                    root.expanded = next
                }
                Accessible.name: group.modelData.title
                background: Item {}
                contentItem: RowLayout {
                    spacing: 10
                    Text {
                        Layout.fillWidth: true
                        text: group.modelData.title
                        color: group.modelData.closed ? theme.muted : theme.text
                        font.family: theme.ui
                        font.pixelSize: 15
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                    }
                    Chip {
                        text: group.modelData.badge
                        textColor: group.modelData.soon ? theme.gold : theme.muted
                        fill: group.modelData.soon ? theme.goldSoft : theme.surface2
                    }
                    Glyph {
                        visible: group.index > 0
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        kind: group.open ? "chevronUp" : "chevronDown"
                        color: theme.faint
                    }
                }
            }

            // Open: a row per building.
            Repeater {
                model: group.open ? group.modelData.buildings : []

                Item {
                    id: building
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: buildingRow.implicitHeight + 18

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: theme.line
                    }

                    RowLayout {
                        id: buildingRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12

                        Rectangle {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            radius: 14
                            color: building.modelData.favorite ? theme.goldSoft : theme.surface2
                            Text {
                                anchors.centerIn: parent
                                text: building.modelData.code.length > 0
                                      ? building.modelData.code
                                      : building.modelData.name.substring(0, 2).toUpperCase()
                                color: building.modelData.favorite ? theme.gold : theme.text
                                font.family: theme.display
                                font.pixelSize: 13
                                font.weight: Font.ExtraBold
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 0
                            spacing: 2
                            Text {
                                Layout.fillWidth: true
                                text: building.modelData.name
                                color: group.modelData.closed ? theme.muted : theme.text
                                font.family: theme.ui
                                font.pixelSize: 14
                                font.weight: Font.Bold
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: group.modelData.closed ? building.modelData.closesText : building.modelData.note
                                color: theme.muted
                                font.family: theme.ui
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                        IconButton {
                            glyph: building.modelData.favorite ? "star" : "starOutline"
                            iconColor: building.modelData.favorite ? theme.gold : theme.faint
                            iconSize: 17
                            flat: true
                            implicitWidth: 40
                            implicitHeight: 40
                            text: building.modelData.favorite ? "Remove from your places" : "Add to your places"
                            onClicked: campus.toggleFavorite(building.modelData.name)
                        }
                    }
                }
            }

            // Closed up: the names, as chips.
            Flow {
                visible: !group.open
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: group.open ? [] : group.modelData.buildings
                    Chip {
                        required property var modelData
                        text: modelData.name
                        textColor: group.modelData.closed ? theme.muted : theme.text
                    }
                }
            }
        }
    }

    Text {
        visible: campus.groups.length === 0
        Layout.fillWidth: true
        Layout.topMargin: 8
        horizontalAlignment: Text.AlignHCenter
        text: "No buildings match."
        color: theme.muted
        font.family: theme.ui
        font.pixelSize: 14
    }

    Text {
        Layout.fillWidth: true
        Layout.topMargin: 4
        text: "Academic buildings lock at 6 PM and are key card only after that unless noted. "
              + "Hours over breaks and holidays can differ."
        color: theme.faint
        font.family: theme.ui
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }
}
