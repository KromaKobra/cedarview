// One dollar balance, as half of a pair.
//
// A component rather than two copies because the pairing is the point: the two
// flex balances are different money with different expiry, and the only way to
// read them correctly is to see them side by side, identical in every respect
// except the caption and the footnote. Divergence between the two tiles would
// imply a difference that is not there.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Card {
    id: tile

    property string caption
    //: Pre-formatted by MealPlan.money(), and "" when Self-Service did not
    //: report it. Never "$0.00" for an unknown balance.
    property string amount
    property string footnote
    property color dotColor: theme.accent

    Theme { id: theme }

    Layout.fillWidth: true
    // Equal halves regardless of how long the amounts are: preferredWidth is
    // what a fillWidth RowLayout divides in proportion to, so two tiles asking
    // for the same 1 get the same half each.
    Layout.preferredWidth: 1
    padding: 14

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Rectangle {
                width: 7
                height: 7
                radius: 3.5
                color: tile.dotColor
            }

            Label {
                Layout.fillWidth: true
                text: tile.caption
                color: theme.muted
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 8
            text: tile.amount.length > 0 ? tile.amount : "—"
            color: theme.text
            font.pixelSize: 24
            font.bold: true
            elide: Text.ElideRight
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 6
            text: tile.footnote
            color: theme.faint
            font.pixelSize: 10
            elide: Text.ElideRight
        }
    }
}
