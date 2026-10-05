// A pill: a status ("in 12 min"), a tag ("Part 1 of 2"), or — with
// `interactive` — a filter or toggle to tap. Colours are given by the caller
// as a hue's text and soft fill (theme.gold and theme.goldSoft, …); an
// `outlined` chip sits on the page with a hairline instead.

import QtQuick
import QtQuick.Controls

AbstractButton {
    id: chip

    property color textColor: theme.muted
    property color fill: theme.surface2
    property bool outlined: false
    property string glyph
    property real fontSize: 12
    property bool interactive: false
    // A selected filter: the cedar pair, whatever the colours given.
    property bool selected: false

    Theme { id: theme }

    enabled: interactive
    hoverEnabled: false
    implicitHeight: 26
    implicitWidth: row.implicitWidth + leftPadding + rightPadding
    leftPadding: 10
    rightPadding: 10
    focusPolicy: Qt.NoFocus
    Accessible.role: interactive ? Accessible.Button : Accessible.StaticText
    Accessible.name: text

    background: Rectangle {
        radius: height / 2
        color: chip.selected ? theme.cedarSoft : chip.fill
        border.width: chip.outlined && !chip.selected ? 1 : 0
        border.color: theme.line
        opacity: chip.down ? 0.7 : 1
    }

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight

        Row {
            id: row
            anchors.centerIn: parent
            spacing: 6

            Glyph {
                visible: chip.glyph.length > 0
                anchors.verticalCenter: parent.verticalCenter
                width: chip.fontSize + 2
                height: chip.fontSize + 2
                kind: chip.glyph
                color: label.color
            }

            Text {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                text: chip.text
                color: chip.selected ? theme.cedar : chip.textColor
                font.family: theme.ui
                font.pixelSize: chip.fontSize
                font.weight: Font.DemiBold
            }
        }
    }
}
