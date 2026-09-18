// The Dining tab — not built yet.
//
// What goes here: every sitting for one day, and paging forward through the
// week. The DiningViewModel already does all of it — `dining.items` is the
// day's full menu, and `nextDay()`/`previousDay()` page through a week that is
// fetched in a single request — so this screen is a QML job with no Python
// behind it. The summary screen shows only the sitting you are about to eat.

import QtQuick

ComingSoon {
    title: "DINING"
    note: "Every meal today, and the menu for the rest of the week."
}
