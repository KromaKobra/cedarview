// The Dining tab: meal plan, Home Cooking menu and dining hours, switched by a
// bar pinned above them.
//
// A StackLayout, not a SwipeView: the pages already sit in the window's
// SwipeView, and a nested one would take every horizontal drag, so you could
// no longer swipe from here back to Chapel.

import QtQuick
import QtQuick.Layouts

Item {
    id: root

    property int section: 0
    signal sectionRequested(int index)

    Theme { id: theme }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        SegmentedControl {
            Layout.fillWidth: true
            Layout.leftMargin: theme.pageMargin
            Layout.rightMargin: theme.pageMargin
            Layout.topMargin: 12
            Layout.bottomMargin: 2
            labels: ["Meal plan", "Menu", "Dining hours"]
            currentIndex: root.section
            selectedColor: theme.cedar
            fontSize: 12
            onActivated: (index) => root.sectionRequested(index)
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.section

            DiningView {}
            ChucksView {}
            HoursView {}
        }
    }
}
