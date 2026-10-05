// One tab's scrolling content, with pull-to-refresh.
//
// Pulling down far enough and letting go asks SyncCoordinator for everything,
// whichever tab it happens on: a reader pulling down means "make this
// current", and the coordinator knows what that takes (and what is already
// in flight). Content goes in like a Card's.

import QtQuick
import QtQuick.Layouts

Flickable {
    id: page

    default property alias content: column.data
    property real margin: theme.pageMargin
    property real topPadding: 4
    property real bottomPadding: 28

    Theme { id: theme }

    readonly property real pull: Math.max(0, -contentY)
    readonly property bool armed: pull > 72

    contentHeight: column.implicitHeight + topPadding + bottomPadding
    clip: true
    boundsBehavior: Flickable.DragOverBounds
    onDragEnded: {
        if (armed)
            sync.refreshAll()
    }

    // The pull indicator, in the gap the pull opens.
    Rectangle {
        visible: page.pull > 4
        x: (page.width - width) / 2
        y: page.contentY + Math.max(6, (page.pull - height) / 2)
        width: 36
        height: 36
        radius: 18
        color: page.armed ? theme.cedarSoft : theme.surface
        border.width: 1
        border.color: theme.line

        Glyph {
            anchors.centerIn: parent
            width: 18
            height: 18
            kind: "refresh"
            color: page.armed ? theme.cedar : theme.faint
            rotation: Math.min(page.pull, 120) * 2.5
        }
    }

    ColumnLayout {
        id: column
        x: page.margin
        y: page.topPadding
        width: page.width - page.margin * 2
        spacing: theme.gap
    }
}
