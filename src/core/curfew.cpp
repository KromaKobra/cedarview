#include "curfew.h"

namespace mycu {

QDateTime curfewFor(QDate night)
{
    const int weekday = night.dayOfWeek();
    if (weekday == Qt::Friday || weekday == Qt::Saturday)
        return QDateTime(night.addDays(1), QTime(0, 59));
    return QDateTime(night, QTime(23, 59));
}

QDate curfewNight(const QDateTime &now)
{
    const QDate today = now.date();
    const QDate yesterday = today.addDays(-1);
    if (now < curfewFor(yesterday))
        return yesterday;
    if (now < curfewFor(today))
        return today;
    return today.addDays(1);
}

} // namespace mycu
