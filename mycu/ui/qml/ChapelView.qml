// The Chapel tab — not built yet.
//
// What goes here: the skip ledger (every movement against the balance, which
// the ChapelViewModel already exposes as `chapel.records`) and a calendar of
// upcoming chapels. The summary screen shows the two figures you check daily —
// how many skips are left, and what tomorrow's chapel is — and this tab is for
// the question that comes after those: *which* chapels did I miss, and what is
// coming up after the next one.
//
// The viewmodel is unchanged and still loads: `chapel.records` is populated on
// every refresh, so building this screen is a QML job with no Python behind it.

import QtQuick

ComingSoon {
    title: "CHAPEL"
    note: "Your skip history and a calendar of upcoming chapels."
}
