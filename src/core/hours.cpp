#include "hours.h"

namespace mycu::hours {

namespace {

using W = std::optional<Window>;

constexpr Window w(int open, int close)
{
    return Window{open, close};
}

} // namespace

QDateTime at(QDate day, int minute)
{
    return QDateTime(day.addDays(minute / 1440), QTime(0, 0).addSecs((minute % 1440) * 60));
}

// ---- Dining ----------------------------------------------------------------

DiningDay diningDayOf(QDate day)
{
    switch (day.dayOfWeek()) {
    case 6:
        return DiningDay::Saturday;
    case 7:
        return DiningDay::Sunday;
    default:
        return DiningDay::Weekday;
    }
}

QString diningDayLabel(DiningDay day)
{
    switch (day) {
    case DiningDay::Saturday:
        return QStringLiteral("Sat");
    case DiningDay::Sunday:
        return QStringLiteral("Sun");
    case DiningDay::Weekday:
        break;
    }
    return QStringLiteral("Mon–Fri");
}

QList<Sitting> commonsSittings(DiningDay day)
{
    // A missing sitting is left out, not shown as "N/A".
    switch (day) {
    case DiningDay::Weekday:
        return {{QStringLiteral("Hot breakfast"), QStringLiteral("breakfast"), w(420, 495)},
                {QStringLiteral("Continental breakfast"), QStringLiteral("breakfast"), w(495, 570)},
                {QStringLiteral("Lunch"), QStringLiteral("lunch"), w(630, 870)},
                {QStringLiteral("Dinner"), QStringLiteral("dinner"), w(990, 1170)}};
    case DiningDay::Saturday:
        return {{QStringLiteral("Continental breakfast"), QStringLiteral("breakfast"), w(480, 540)},
                {QStringLiteral("Brunch"), QStringLiteral("lunch"), w(660, 780)},
                {QStringLiteral("Dinner"), QStringLiteral("dinner"), w(990, 1110)}};
    case DiningDay::Sunday:
        return {{QStringLiteral("Hot breakfast"), QStringLiteral("breakfast"), w(480, 540)},
                {QStringLiteral("Lunch"), QStringLiteral("lunch"), w(690, 840)},
                {QStringLiteral("Dinner"), QStringLiteral("dinner"), w(1020, 1170)}};
    }
    return {};
}

DayWindows commonsOn(QDate day)
{
    DayWindows windows;
    for (const Sitting &sitting : commonsSittings(diningDayOf(day)))
        windows.append(sitting.hours);
    return windows;
}

DayWindows Venue::on(QDate day) const
{
    const W &window = hours[static_cast<int>(diningDayOf(day))];
    return window ? DayWindows{*window} : DayWindows{};
}

const QList<Venue> &venues()
{
    // Per DiningDay: Mon–Fri, Sat, Sun.
    static const QList<Venue> table = {
        {QStringLiteral("The Commons Market"), QStringLiteral("Grab+Go meals at lunch and dinner"),
         {w(630, 1380), w(660, 1320), w(690, 1380)}, {}},
        {QStringLiteral("The Commons Express"), QStringLiteral("Pizza and tacos"),
         {w(1200, 1380), std::nullopt, w(1200, 1380)}, {}},
        {QStringLiteral("Rinnova"), QString(), {w(450, 1140), w(630, 960), std::nullopt}, {}},
        {QStringLiteral("Chick-fil-A / Tossed"), QString(), {w(630, 1260), w(630, 1260), std::nullopt},
         {w(630, 1200), w(630, 1200), std::nullopt}},
        {QStringLiteral("Panda Express"), QString(), {w(630, 1260), w(630, 1260), std::nullopt},
         {w(630, 1200), w(630, 1200), std::nullopt}},
        {QStringLiteral("The Cafe"), QString(), {w(630, 1260), std::nullopt, w(840, 1260)},
         {w(630, 1200), std::nullopt, w(840, 1200)}},
        {QStringLiteral("Grab+Go Market (BTS)"), QString(), {w(630, 810), std::nullopt, std::nullopt}, {}},
    };
    return table;
}

const QList<SwipePeriod> &swipePeriods()
{
    // Dinner runs to close; 1440 stands in for "close".
    static const QList<SwipePeriod> table = {
        {QStringLiteral("Breakfast"), w(420, 600), QStringLiteral("7:00 – 9:59 AM")},
        {QStringLiteral("Lunch"), w(600, 960), QStringLiteral("10:00 AM – 3:59 PM")},
        {QStringLiteral("Dinner"), w(960, 1440), QStringLiteral("4:00 PM – close")},
    };
    return table;
}

// ---- Buildings -------------------------------------------------------------

BuildingDay buildingDayOf(QDate day)
{
    switch (day.dayOfWeek()) {
    case 5:
        return BuildingDay::Friday;
    case 6:
        return BuildingDay::Saturday;
    case 7:
        return BuildingDay::Sunday;
    default:
        return BuildingDay::MonThu;
    }
}

QString buildingDayLabel(BuildingDay day)
{
    switch (day) {
    case BuildingDay::Friday:
        return QStringLiteral("Fri");
    case BuildingDay::Saturday:
        return QStringLiteral("Sat");
    case BuildingDay::Sunday:
        return QStringLiteral("Sun");
    case BuildingDay::MonThu:
        break;
    }
    return QStringLiteral("Mon–Thu");
}

DayWindows Building::on(QDate day) const
{
    const W &window = hours[static_cast<int>(buildingDayOf(day))];
    return window ? DayWindows{*window} : DayWindows{};
}

const QList<Building> &buildings()
{
    // Per BuildingDay: Mon–Thu, Fri, Sat, Sun.
    static const QList<Building> table = {
        {QStringLiteral("Alford Annex"), QStringLiteral("AA"),
         QStringLiteral("Access card only after 5 PM and on weekends"),
         {w(360, 1380), w(360, 1380), w(420, 1380), w(720, 1380)}},
        {QStringLiteral("Alford Auditorium"), QStringLiteral("AL"),
         QStringLiteral("Authorized access only after 6 PM and on weekends"),
         {w(420, 1380), w(420, 1380), std::nullopt, std::nullopt}},
        {QStringLiteral("Apple Technology Resource Center"), QStringLiteral("APP"), QString(),
         {w(420, 1425), w(420, 1485), w(420, 1485), w(720, 1425)}},
        {QStringLiteral("Callan Athletic Center"), QStringLiteral("CAL"), QString(),
         {w(360, 1380), w(360, 1320), w(420, 1320), w(840, 1080)}},
        {QStringLiteral("Carnegie Center for the Visual Arts"), QStringLiteral("CNG"),
         QStringLiteral("Access card only after 6 PM and on weekends"),
         {w(420, 1380), w(420, 1380), w(420, 1380), w(420, 1380)}},
        {QStringLiteral("Centennial Library"), QStringLiteral("LB"), QString(),
         {w(465, 1410), w(465, 1140), w(600, 1140), w(930, 1410)}},
        {QStringLiteral("Center for Biblical and Theological Studies"), QStringLiteral("BTS"), QString(),
         {w(390, 1425), w(390, 1485), w(390, 1485), w(720, 1425)}},
        {QStringLiteral("Chemistry Lab Center"), QString(),
         QStringLiteral("Authorized access cards only after 6 PM and on weekends"),
         {w(420, 1080), w(420, 1080), std::nullopt, std::nullopt}},
        {QStringLiteral("Chick-fil-A"), QString(),
         QStringLiteral("The restaurant closes at 9 PM except Sundays; the space stays open to students"),
         {w(420, 1410), w(420, 1410), w(420, 1410), w(720, 1410)}},
        {QStringLiteral("Civil Engineering Center"), QString(), QString(),
         {w(420, 1050), w(420, 1050), std::nullopt, std::nullopt}},
        {QStringLiteral("Dixon Ministry Center"), QStringLiteral("DMC"), QString(),
         {w(420, 1380), w(420, 1380), w(420, 1380), w(720, 1380)}},
        {QStringLiteral("Engineering and Science Center"), QStringLiteral("ENS"),
         QStringLiteral("Engineering students use access cards after 6 PM and on weekends"),
         {w(360, 1425), w(360, 1485), w(420, 1485), w(720, 1425)}},
        {QStringLiteral("Engineering Projects Laboratory"), QStringLiteral("EPL"),
         QStringLiteral("Engineering students use access cards after 6 PM and on weekends"),
         {w(360, 1380), w(360, 1380), w(420, 1380), w(720, 1380)}},
        {QStringLiteral("Fitness Recreation Center"), QStringLiteral("FTR"), QString(),
         {w(360, 1380), w(360, 1320), w(600, 1320), w(840, 1200)}},
        {QStringLiteral("Health Sciences Center"), QStringLiteral("HSC"),
         QStringLiteral("Pharmacy and nursing students use access cards after 6 PM"),
         {w(390, 1425), w(390, 1485), w(390, 1485), w(390, 1425)}},
        {QStringLiteral("Milner Hall"), QString(), QString(),
         {w(420, 1425), w(420, 1485), w(720, 1485), w(900, 1425)}},
        {QStringLiteral("Scharnberg Business and Communication Center"), QStringLiteral("SBCC"), QString(),
         {w(420, 1425), w(420, 1485), w(420, 1485), w(720, 1425)}},
        {QStringLiteral("Stevens Student Center"), QStringLiteral("SSC"), QString(),
         {w(390, 1425), w(390, 1485), w(390, 1485), w(390, 1425)}},
        {QStringLiteral("Tyler Digital Communication Center"), QStringLiteral("TYL"), QString(),
         {w(420, 1380), w(420, 1380), w(600, 1380), w(720, 1380)}},
    };
    return table;
}

// ---- Asking a schedule about the clock ------------------------------------

std::optional<Opening> currentOpening(const Schedule &schedule, const QDateTime &now)
{
    const QDate today = now.date();
    const int minute = now.time().hour() * 60 + now.time().minute();

    for (const Window &window : schedule(today)) {
        if (window.contains(minute))
            return Opening{at(today, window.open), at(today, window.close)};
    }
    // Still inside last night's window, past midnight.
    const QDate yesterday = today.addDays(-1);
    for (const Window &window : schedule(yesterday)) {
        if (window.close > 1440 && window.contains(minute + 1440))
            return Opening{at(yesterday, window.open), at(yesterday, window.close)};
    }
    return std::nullopt;
}

bool isOpen(const Schedule &schedule, const QDateTime &now)
{
    return currentOpening(schedule, now).has_value();
}

std::optional<QDateTime> closesAt(const Schedule &schedule, const QDateTime &now)
{
    const auto opening = currentOpening(schedule, now);
    return opening ? std::optional<QDateTime>(opening->closes) : std::nullopt;
}

std::optional<Opening> nextOpening(const Schedule &schedule, const QDateTime &now, int days)
{
    for (int offset = 0; offset <= days; ++offset) {
        const QDate day = now.date().addDays(offset);
        for (const Window &window : schedule(day)) {
            const QDateTime opens = at(day, window.open);
            if (opens > now)
                return Opening{opens, at(day, window.close)};
        }
    }
    return std::nullopt;
}

} // namespace mycu::hours
