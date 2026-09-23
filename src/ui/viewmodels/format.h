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
