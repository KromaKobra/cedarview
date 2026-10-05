// One chapel's details, in a sheet: who, when and how long from now, what
// missing it costs, and Watch live.
//
// `details` is what the tapped row or hero knew: {who, subtitle, description,
// dateText, timeText, startsAt, livestream, youtubeId, isToday}. Nothing is
// fetched for it — the speaker's preview image in the feed is left alone, as
// it would mean contacting a host that is not Cedarville's.

import QtQuick
import QtQuick.Layouts

BottomSheet {
    id: sheet

    property var details: ({})

    Theme { id: theme }

    function show(chapelDetails) {
        details = chapelDetails
        open()
    }

    readonly property string fromNow: details.startsAt ? chapel.fromNowText(details.startsAt) : ""
    readonly property bool upcoming: fromNow.length > 0 && fromNow !== "Now"

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text {
            Layout.fillWidth: true
            text: (sheet.details.isToday ? "Today's chapel" : "Chapel").toUpperCase()
            color: theme.gold
            font.family: theme.ui
            font.pixelSize: 11
            font.weight: Font.Bold
            font.letterSpacing: 1.1
        }
        IconButton {
            glyph: "close"
            iconSize: 18
            implicitWidth: 40
            implicitHeight: 40
            radius: 14
            fill: theme.surface2
            text: "Close"
            onClicked: sheet.close()
        }
    }

    // The livestream, as a panel: Watch live when there is one.
    HeroCard {
        Layout.topMargin: 10
        radius: 22
        raised: false
        padding: 16

        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            Rectangle {
                Layout.preferredWidth: 52
                Layout.preferredHeight: 52
                radius: 26
                color: Qt.rgba(1, 1, 1, 0.14)
                Glyph {
                    anchors.centerIn: parent
                    anchors.horizontalCenterOffset: 2
                    width: 24
                    height: 24
                    kind: sheet.details.livestream ? "play" : "chapel"
                    color: theme.navyText
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    Layout.fillWidth: true
                    text: sheet.details.livestream ? "Livestream at " + sheet.details.timeText : "Not livestreamed"
                    color: theme.navyText
                    font.family: theme.ui
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    text: sheet.details.livestream ? "On YouTube, in your browser" : "No livestream listed for this one"
                    color: theme.navyMuted
                    font.family: theme.ui
                    font.pixelSize: 13
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        Layout.topMargin: 16
        text: sheet.details.who || ""
        color: theme.text
        font.family: theme.display
        font.pixelSize: 28
        font.weight: Font.Bold
        wrapMode: Text.Wrap
    }
    Text {
        Layout.fillWidth: true
        Layout.topMargin: 6
        visible: text.length > 0
        text: sheet.details.subtitle ? sheet.details.subtitle : (sheet.details.description || "")
        color: theme.muted
        font.family: theme.ui
        font.pixelSize: 14
        lineHeight: 1.15
        wrapMode: Text.Wrap
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: 16
        implicitHeight: 64
        radius: 18
        color: theme.surface2

        RowLayout {
            anchors.fill: parent
            spacing: 0
            Repeater {
                model: [
                    { figure: sheet.details.dateText || "", label: "Date", hot: false },
                    { figure: sheet.details.timeText || "", label: "Starts", hot: false },
                    { figure: sheet.fromNow.length > 0 ? sheet.fromNow : "Over", label: "From now", hot: sheet.upcoming }
                ]
                Item {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1

                    Rectangle {
                        visible: parent.index > 0
                        width: 1
                        height: parent.height - 24
                        anchors.verticalCenter: parent.verticalCenter
                        color: theme.line
                    }
                    Column {
                        anchors.centerIn: parent
                        spacing: 2
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: parent.parent.modelData.figure
                            color: parent.parent.modelData.hot ? theme.gold : theme.text
                            font.family: theme.display
                            font.pixelSize: 17
                            font.weight: Font.Bold
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: parent.parent.modelData.label
                            color: theme.faint
                            font.family: theme.ui
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }
    }

    RowLayout {
        visible: sheet.upcoming && chapel.remaining >= 0 && chapel.requiredToAttend
        Layout.fillWidth: true
        Layout.topMargin: 14
        spacing: 8
        Glyph {
            Layout.preferredWidth: 17
            Layout.preferredHeight: 17
            kind: "info"
            color: theme.gold
        }
        Text {
            Layout.fillWidth: true
            text: chapel.remaining > 0
                  ? "Missing it uses 1 of your " + chapel.remaining + " remaining skips."
                  : "You have no skips left to miss it with."
            color: theme.muted
            font.family: theme.ui
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
    }

    PrimaryButton {
        visible: (sheet.details.youtubeId || "").length > 0
        Layout.fillWidth: true
        Layout.topMargin: 18
        glyph: "play"
        text: "Watch live"
        onClicked: Qt.openUrlExternally(chapel.watchUrl(sheet.details.youtubeId))
    }
}
