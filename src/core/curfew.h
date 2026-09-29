// Residence-hall curfew — when it falls on a given night.
//
// **Hand-entered, like the term dates in calendar.h.** Nothing Cedarville
// publishes carries it. The rule:
//
//     Sunday–Thursday nights    11:59 PM
//     Friday, Saturday nights   12:59 AM, i.e. the next morning
//
// A "night" is named after the evening it starts on. Friday night's curfew is
// at 12:59 AM on Saturday, so at 12:30 AM on Saturday the curfew still ahead of
// you is Friday's, not Saturday's.

#pragma once

#include <QDate>
#include <QDateTime>

namespace mycu {

// When curfew falls for the night that begins on `night`, in local time.
QDateTime curfewFor(QDate night);

// The night whose curfew is the next one to come at `now`.
//
// Yesterday while it's still before yesterday's curfew (only Friday and
// Saturday nights run past midnight), otherwise today. Once curfew has passed
// the answer moves straight on to the following night; there is no "curfew
// has passed" state.
QDate curfewNight(const QDateTime &now);

} // namespace mycu
