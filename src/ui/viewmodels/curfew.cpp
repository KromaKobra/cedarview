#include "curfew.h"

#include "core/curfew.h"
#include "format.h"

namespace mycu {

CurfewViewModel::CurfewViewModel(QObject *parent)
    : QObject(parent)
    , m_now(QDateTime::currentDateTime())
{}

QString CurfewViewModel::timeText() const
{
    return fmt::clock(curfewFor(curfewNight(m_now)).time());
}

QString CurfewViewModel::nightText() const
{
    return fmt::dayLong(curfewNight(m_now)) + QStringLiteral(" night");
}

bool CurfewViewModel::lateNight() const
{
    const QDate night = curfewNight(m_now);
    return curfewFor(night).date() > night;
}

QString CurfewViewModel::countdownText() const
{
    const qint64 left = m_now.secsTo(curfewFor(curfewNight(m_now)));
    if (left <= 0 || left > 3600)
        return {};
    // 3600 exactly is still "60:00", not "1:00:00".
    return QStringLiteral("%1:%2").arg(left / 60).arg(left % 60, 2, 10, QLatin1Char('0'));
}

void CurfewViewModel::refreshAll()
{
    const QDateTime current = now();
    // Seconds are all the countdown shows, so a same-second call changes nothing.
    if (current.toSecsSinceEpoch() == m_now.toSecsSinceEpoch())
        return;
    m_now = current;
    emit changed();
}

} // namespace mycu
