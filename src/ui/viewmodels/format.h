// Date and time text for the viewmodels.
//
// Always the C locale's names — "Mon", "Sep", "AM" — whatever language the
// phone is in, because every string around them on screen is English. Built
// from QLocale::c() rather than the system locale for the same reason.
//
// Hours are 12-hour with no leading zero ("7:05 PM", "12:00 PM"), and days of
// the month have no leading zero ("Sep 5"), which is how a person writes them.

#pragma once

#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QString>

#include <algorithm>

namespace mycu::fmt {

// "Mon"
inline QString dayShort(QDate day) { return QLocale::c().toString(day, QStringLiteral("ddd")); }
// "Monday"
inline QString dayLong(QDate day) { return QLocale::c().toString(day, QStringLiteral("dddd")); }
// "Sep"
inline QString monthShort(QDate day) { return QLocale::c().toString(day, QStringLiteral("MMM")); }
// "September"
inline QString monthLong(QDate day) { return QLocale::c().toString(day, QStringLiteral("MMMM")); }

// "7:05 PM" / "12:00 PM" / "12:05 AM".
inline QString clock(QTime at)
{
    const int hour = at.hour() % 12 == 0 ? 12 : at.hour() % 12;
    return QStringLiteral("%1:%2 %3")
        .arg(hour)
        .arg(at.minute(), 2, 10, QLatin1Char('0'))
        .arg(at.hour() < 12 ? QStringLiteral("AM") : QStringLiteral("PM"));
}

// "8 PM" / "10:30 AM" — the minutes only when there are some, as a sign
// would put it.
inline QString clockShort(QTime at)
{
    const int hour = at.hour() % 12 == 0 ? 12 : at.hour() % 12;
    const QString minutes = at.minute() ? QStringLiteral(":%1").arg(at.minute(), 2, 10, QLatin1Char('0'))
                                        : QString();
    return QString::number(hour) + minutes + (at.hour() < 12 ? QStringLiteral(" AM") : QStringLiteral(" PM"));
}

// "7:00" / "2:30" — a time with no AM or PM, for where a range already says
// which half of the day it is in.
inline QString clockBare(QTime at)
{
    const int hour = at.hour() % 12 == 0 ? 12 : at.hour() % 12;
    return QStringLiteral("%1:%2").arg(hour).arg(at.minute(), 2, 10, QLatin1Char('0'));
}

// "18 min" / "2 h 5 min" / "3 days" — a span of time, rounded up to the minute.
inline QString span(qint64 secs)
{
    const qint64 minutes = (std::max<qint64>(0, secs) + 59) / 60;
    if (minutes < 60)
        return QString::number(minutes) + QStringLiteral(" min");
    if (minutes < 24 * 60) {
        const qint64 h = minutes / 60;
        const qint64 m = minutes % 60;
        return QString::number(h) + QStringLiteral(" h") + (m ? QStringLiteral(" %1 min").arg(m) : QString());
    }
    const qint64 days = (minutes + 12 * 60) / (24 * 60);
    return QString::number(days) + (days == 1 ? QStringLiteral(" day") : QStringLiteral(" days"));
}

// "1h 11m" — a countdown, compact enough for a hero figure.
inline QString countdown(qint64 secs)
{
    const qint64 minutes = (std::max<qint64>(0, secs) + 59) / 60;
    if (minutes < 60)
        return QString::number(minutes) + u'm';
    return QStringLiteral("%1h %2m").arg(minutes / 60).arg(minutes % 60);
}

// "Mon, Sep 14" / "Fri, Dec 11".
inline QString shortDate(QDate day)
{
    return dayShort(day) + QStringLiteral(", ") + monthShort(day) + u' ' + QString::number(day.day());
}

// "Sep 14".
inline QString monthDay(QDate day)
{
    return monthShort(day) + u' ' + QString::number(day.day());
}

} // namespace mycu::fmt
