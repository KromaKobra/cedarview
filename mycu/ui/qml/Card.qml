// A rounded panel. Everything on the summary screen sits in one of these.
//
// Sizes itself to its content: put a ColumnLayout in `content` and the card's
// height follows. That is the whole reason this exists as a component — a
// screen of Rectangles each carrying its own `Layout.preferredHeight:
// something.implicitHeight + 32` is a screen where one forgotten +32 clips a
// line of text off the bottom of a card.

import QtQuick
import QtQuick.Layouts

Rectangle {
    id: card

    // Where callers put their content. Declared as the default property so a
    // caller writes `Card { ColumnLayout { … } }` and not `Card { content:
    // ColumnLayout { … } }`.
    default property alias content: inner.data
    property int padding: theme.cardPadding

    Theme { id: theme }

    Layout.fillWidth: true
    implicitHeight: inner.implicitHeight + padding * 2
    radius: theme.cardRadius
    color: theme.card
    border.width: 1
    border.color: theme.cardBorder

    ColumnLayout {
        id: inner
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: card.padding
        spacing: 0
    }
}
