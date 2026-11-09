// Dining › Menu: one page per place to eat, switched by a segmented control —
// The Commons' full menu, and The Commons Express, which orders online.

import QtQuick
import QtQuick.Layouts

Item {
    id: root

    property int venue: 0
    signal venueRequested(int index)

    Theme { id: theme }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        SegmentedControl {
            Layout.fillWidth: true
            Layout.leftMargin: theme.pageMargin
            Layout.rightMargin: theme.pageMargin
            Layout.bottomMargin: 10
            labels: ["The Commons", "The Commons Express"]
            currentIndex: root.venue
            onActivated: (index) => root.venueRequested(index)
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.venue

            MenuView {}
            CommonsExpressView {}
        }
    }
}
