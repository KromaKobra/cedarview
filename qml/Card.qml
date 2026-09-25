// A rounded panel. Everything on the summary screen sits in one of these.
//
// Sizes itself to its content: put a ColumnLayout in `content` and the card's
// height follows. That is the whole reason this exists as a component — a
// screen of Rectangles each carrying its own `Layout.preferredHeight:
// something.implicitHeight + 32` is a screen where one forgotten +32 clips a
// line of text off the bottom of a card.
//
// Optionally tappable: set `tappable` and handle `tapped`. A TapHandler rather
// than a MouseArea, so a drag that starts on a card still scrolls the page (and
// still pulls to refresh) instead of being swallowed as a press.

import QtQuick
import QtQuick.Layouts

Rectangle {
    id: card

    // Where callers put their content. Declared as the default property so a
    // caller writes `Card { ColumnLayout { … } }` and not `Card { content:
    // ColumnLayout { … } }`.
    default property alias content: inner.data
    property int padding: theme.cardPadding
    property bool tappable: false

    signal tapped()

    Theme { id: theme }

    Layout.fillWidth: true
    implicitHeight: inner.implicitHeight + padding * 2
    radius: theme.cardRadius
    color: theme.card
    border.width: 1
    border.color: theme.cardBorder

    Behavior on color { ColorAnimation { duration: 180 } }

    TapHandler {
        id: tap
        enabled: card.tappable
        onTapped: card.tapped()
    }

    // The press highlight. Declared before `inner` so it sits under the
    // content rather than washing over it.
    Rectangle {
        anchors.fill: parent
        radius: card.radius
        color: tap.pressed ? theme.pressed : "transparent"
    }

    ColumnLayout {
        id: inner
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: card.padding
        spacing: 0
    }
}
