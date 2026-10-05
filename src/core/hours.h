// Opening hours: The Commons' sittings, the other dining venues, the meal-swipe
// periods and every campus building.
//
// **Hand-entered, like calendar.h and curfew.h.** Nothing Cedarville publishes
// carries these as data. They are copied from two pages:
//
//   dining   https://www.cedarville.edu/offices/the-commons/dining-information
//            (2026-09-28)
//   buildings https://www.cedarville.edu/offices/campus-security/building-hours
//            (2026-09-29)
//
// and lived in HoursView.qml and BuildingsView.qml until v0.4, when the Today
// screen, search and the Campus tab all needed to ask "is it open?" of the same
// tables. If a page changes, change it here. Chuck's sittings also drive
// servingHours() in providers/dining.cpp, which picks the summary's next meal;
// a change to The Commons' hours belongs in both places.
//
// Times are minutes after midnight. A window that runs past midnight ends after
// 1440 — 12:45 AM is 1485 — which is how a building is still open at 12:30 AM on
// Saturday under Friday's hours.
//
// Two cells on the buildings page are not taken literally: BTS's Friday opening
// says "6:30 p.m." between 6:30 a.m. on every other day, and is read as a typo;
// Alford Auditorium's Saturday is only an asterisk pointing at its
// authorized-access note, and is shown as closed with that note.
//
// Everything below is a pure function of the clock it is handed.

#pragma once

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>

#include <array>
#include <functional>
#include <optional>

namespace mycu::hours {

// One opening, in minutes after midnight of the day it starts on.
struct Window
{
    int open = 0;
    int close = 0;

    bool contains(int minute) const { return open <= minute && minute < close; }
    bool operator==(const Window &) const = default;
};

// Whatever is open on a given date: none, one, or (The Commons) several.
using DayWindows = QList<Window>;

// ---- Dining ----------------------------------------------------------------

// The dining page's three columns.
enum class DiningDay { Weekday = 0, Saturday = 1, Sunday = 2 };
DiningDay diningDayOf(QDate day);

// "Mon–Fri" / "Sat" / "Sun".
QString diningDayLabel(DiningDay day);

// One of The Commons' sittings. `slot` matches the menu feed's ("breakfast",
// "lunch", "dinner"), so a sitting can be paired with what is served at it.
struct Sitting
{
    QString name;
    QString slot;
    Window hours;
};

QList<Sitting> commonsSittings(DiningDay day);

// A venue other than The Commons' own sittings. `hours` and `exchange` are per
// DiningDay; nothing is closed / no meal exchange.
struct Venue
{
    QString name;
    QString note;
    std::array<std::optional<Window>, 3> hours;
    std::array<std::optional<Window>, 3> exchange;

    DayWindows on(QDate day) const;
};

const QList<Venue> &venues();

// The Commons itself, as a place: open during any of its sittings.
DayWindows commonsOn(QDate day);

// The card scans once per one of these (block plans, up to five times).
struct SwipePeriod
{
    QString name;
    Window window;
    // "7:00 – 9:59 AM", as the dining page words it.
    QString text;
};

const QList<SwipePeriod> &swipePeriods();

// ---- Buildings -------------------------------------------------------------

// The building-hours page's four columns.
enum class BuildingDay { MonThu = 0, Friday = 1, Saturday = 2, Sunday = 3 };
BuildingDay buildingDayOf(QDate day);

// "Mon–Thu" / "Fri" / "Sat" / "Sun".
QString buildingDayLabel(BuildingDay day);

struct Building
{
    QString name;
    // The page's abbreviation, where it gives one ("LB", "SSC").
    QString code;
    QString note;
    std::array<std::optional<Window>, 4> hours;

    DayWindows on(QDate day) const;
};

const QList<Building> &buildings();

// ---- Asking a schedule about the clock ------------------------------------

// A place's openings, by date. Venue::on, Building::on and commonsOn all fit.
using Schedule = std::function<DayWindows(QDate)>;

// The window `now` falls in — today's, or yesterday's if it runs past
// midnight — as absolute times.
struct Opening
{
    QDateTime opens;
    QDateTime closes;
};

std::optional<Opening> currentOpening(const Schedule &schedule, const QDateTime &now);

bool isOpen(const Schedule &schedule, const QDateTime &now);

// When the current opening ends; nothing when closed.
std::optional<QDateTime> closesAt(const Schedule &schedule, const QDateTime &now);

// The next opening that starts after `now`, looking up to `days` ahead.
std::optional<Opening> nextOpening(const Schedule &schedule, const QDateTime &now, int days = 7);

// An absolute time for minute `minute` of `day` (1485 -> 12:45 AM the next day).
QDateTime at(QDate day, int minute);

} // namespace mycu::hours
