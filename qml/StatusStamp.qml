// "◷ 8:15" in a card's corner: these figures are a saved copy, or old, and
// this is when they were fetched. Bound to one source's SourceStatus; shows
// itself only when that status says the stamp is due.

import QtQuick
import QtQuick.Layouts

RowLayout {
    id: stamp

    property var status: null
    property bool navy: false
    // "as of 8:15 AM" rather than the bare time, where there is room.
    property bool verbose: false

    Theme { id: theme }

    visible: status !== null && status.showStamp
    spacing: 4

    Glyph {
        Layout.preferredWidth: 12
        Layout.preferredHeight: 12
        kind: "clock"
        stroke: 2
        color: stamp.navy ? theme.navyMuted : theme.faint
    }

    Text {
        text: stamp.status ? (stamp.verbose ? "as of " + stamp.status.stampText : stamp.status.stampText) : ""
        color: stamp.navy ? theme.navyMuted : theme.faint
        font.family: theme.ui
        font.pixelSize: 11
        font.weight: Font.DemiBold
    }
}
