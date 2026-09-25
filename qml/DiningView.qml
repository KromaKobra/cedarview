import QtQuick
import QtQuick.Controls
import QtQuick.Effects
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
        onDragEnded: if (contentY < -80) dining.refreshAll()

        ColumnLayout {
            id: column
            x: theme.pageMargin
            y: theme.pageMargin
            width: scroll.width - theme.pageMargin * 2
            spacing: theme.gap

            // Temporary flex expires; permanent flex rolls over. Tones match
            // the summary tab's balance tiles, and permanent flex is likewise
            // only shown when the account has some.
            StatStrip {
                stats: [
                    { value: dining.mealsRemaining >= 0 ? dining.mealsRemaining : "—",
                      label: "meals " + dining.mealsPeriodText, tone: theme.accent },
                    { value: dining.diningDollars.length > 0 ? dining.diningDollars : "—",
                      label: "temp flex", tone: theme.violet },
                    { value: dining.flexDollars, label: "perm flex", tone: theme.cedar }
                ].filter(stat => stat.label !== "perm flex" || dining.hasFlexDollars)
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 2
                Layout.topMargin: 8
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        Layout.fillWidth: true
                        text: "RECENT ACTIVITY"
                        color: theme.faint
                        font.pixelSize: 11
                        font.bold: true
                        font.letterSpacing: 1.4
                        elide: Text.ElideRight
                    }

                    // All | Flex, as a segmented control: both choices are
                    // always visible, and a thumb raised out of a groove slides
                    // to the one showing — neutral for all, violet for flex, to
                    // match the flex rows below.
                    Item {
                        id: flexFilter
                        readonly property real segment: 64
                        readonly property real inset: 3
                        implicitWidth: segment * 2 + inset * 2
                        implicitHeight: 36

                        Rectangle {
                            anchors.fill: parent
                            radius: height / 2
                            border.width: 1
                            border.color: theme.hairline
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: theme.grooveTop }
                                GradientStop { position: 1.0; color: theme.grooveBottom }
                            }
                        }

                        Rectangle {
                            x: dining.flexOnly ? flexFilter.width - width - flexFilter.inset
                                               : flexFilter.inset
                            y: flexFilter.inset
                            width: flexFilter.segment
                            height: flexFilter.height - flexFilter.inset * 2
                            radius: height / 2
                            gradient: Gradient {
                                GradientStop {
                                    position: 0.0
                                    color: dining.flexOnly ? theme.violetTop : theme.raisedTop
                                    Behavior on color { ColorAnimation { duration: 180 } }
                                }
                                GradientStop {
                                    position: 1.0
                                    color: dining.flexOnly ? theme.violetBottom : theme.raisedBottom
                                    Behavior on color { ColorAnimation { duration: 180 } }
                                }
                            }

                            layer.enabled: true
                            layer.effect: MultiEffect {
                                shadowEnabled: true
                                shadowColor: dining.flexOnly ? theme.violet : "#000000"
                                shadowOpacity: dining.flexOnly ? (theme.light ? 0.40 : 0.35)
                                                               : (theme.light ? 0.16 : 0.45)
                                shadowBlur: 0.45
                                shadowVerticalOffset: 2
                            }

                            Behavior on x {
                                NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
                            }
                        }

                        Row {
                            anchors.fill: parent
                            anchors.margins: flexFilter.inset

                            Repeater {
                                model: [{ label: "All", flex: false }, { label: "Flex", flex: true }]

                                AbstractButton {
                                    id: segmentButton
                                    readonly property bool active: dining.flexOnly === modelData.flex
                                    width: flexFilter.segment
                                    height: parent.height
                                    onClicked: dining.setFlexOnly(modelData.flex)
                                    Accessible.name: modelData.flex ? "Show flex purchases only"
                                                                    : "Show all activity"
                                    background: Item {}
                                    contentItem: Label {
                                        text: modelData.label
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        color: !segmentButton.active ? theme.muted
                                               : (modelData.flex ? theme.textOnViolet : theme.text)
                                        opacity: segmentButton.down && !segmentButton.active ? 0.6 : 1.0
                                        font.pixelSize: 13
                                        font.bold: true
                                        Behavior on color { ColorAnimation { duration: 180 } }
                                    }
                                }
                            }
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: dining.activitySummary
                    color: theme.muted
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }

            Card {
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: dining.activityEmptyText
                        color: theme.muted
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        model: dining.activity

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Label {
                                Layout.fillWidth: true
                                Layout.topMargin: model.index === 0 ? 0 : 22
                                visible: model.isHeader
                                text: model.title.toUpperCase()
                                color: theme.cedar
                                font.pixelSize: 10
                                font.bold: true
                                font.letterSpacing: 1.1
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
                                Layout.topMargin: 15
                                visible: !model.isHeader
                                spacing: 12

                                Rectangle {
                                    Layout.alignment: Qt.AlignTop
                                    width: 38
                                    height: 38
                                    radius: 13
                                    color: model.isFlex ? theme.violetSoft : theme.accentSoft

                                    Glyph {
                                        anchors.centerIn: parent
                                        width: 16
                                        height: 16
                                        kind: model.isFlex ? "dining" : "chucks"
                                        color: model.isFlex ? theme.violet : theme.accent
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label {
                                        Layout.fillWidth: true
                                        text: model.title
                                        color: theme.text
                                        font.pixelSize: 13
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        visible: text.length > 0
                                        text: model.detail
                                        color: theme.muted
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                }

                                Label {
                                    Layout.alignment: Qt.AlignTop
                                    visible: text.length > 0
                                    text: model.amount
                                    color: model.isDeposit ? theme.cedar
                                                           : (model.isFlex ? theme.violet : theme.text)
                                    font.pixelSize: 13
                                    font.bold: true
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: dining.error.length > 0
                implicitHeight: diningError.implicitHeight + 30
                radius: 16
                color: theme.dangerSoft
                Label {
                    id: diningError
                    anchors.fill: parent
                    anchors.margins: 15
                    text: dining.error
                    color: theme.danger
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
            }

            Item { Layout.preferredHeight: 8 }
        }
    }
}
