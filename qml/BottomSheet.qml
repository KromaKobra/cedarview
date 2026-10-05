// A sheet that rises from the bottom over a scrim: a chapel's details, the
// More menu, About. Full width, rounded at the top, with a grab handle.
//
// A Popup dressed by hand rather than a Dialog: Dialog's buttons are drawn by
// the Controls style, and one stock button on a sheet the app has painted
// itself would give the whole thing away. Content goes in like a Card's — a
// ColumnLayout's worth — and the sheet sizes to it, up to most of the screen,
// scrolling beyond that.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: sheet

    default property alias content: inner.data

    Theme { id: theme }

    parent: Overlay.overlay
    x: 0
    width: parent ? parent.width : 360
    height: Math.min(inner.implicitHeight + inner.y + topPadding + bottomPadding,
                     parent ? parent.height * 0.92 : 800)
    // How far the sheet has sunk below the bottom edge, 0 to 1. The
    // transitions animate this rather than y: animating y replaces this
    // binding, and every opening after the first then rose to where the last
    // closing had left it — offscreen, with only the scrim to show for it.
    property real sunk: 0
    y: parent ? parent.height - height * (1 - sunk) : 0
    modal: true
    focus: true
    padding: 20
    topPadding: 10
    bottomPadding: 26 + (parent ? parent.SafeArea.margins.bottom : 0)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: Transition {
        NumberAnimation { property: "sunk"; from: 1; to: 0; duration: 260; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "sunk"; to: 1; duration: 200; easing.type: Easing.InCubic }
    }

    background: Rectangle {
        color: theme.surface
        topLeftRadius: 30
        topRightRadius: 30

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 10
            width: 40
            height: 5
            radius: 3
            color: theme.line
        }
    }

    Overlay.modal: Rectangle {
        color: theme.scrim
        Behavior on opacity { NumberAnimation { duration: 180 } }
    }

    contentItem: Flickable {
        clip: true
        contentHeight: inner.implicitHeight + inner.y
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height

        ColumnLayout {
            id: inner
            width: parent.width
            y: 14
            spacing: 0
        }
    }
}
