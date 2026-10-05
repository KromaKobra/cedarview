#include "curfew.h"

#include "core/curfew.h"
#include "format.h"

#include <algorithm>

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

QDateTime CurfewViewModel::curfewAt() const
{
    return curfewFor(curfewNight(m_now));
}

QDateTime CurfewViewModel::eveningStarts() const
{
    return QDateTime(curfewNight(m_now), QTime(20, 0));
}

double CurfewViewModel::eveningFraction() const
{
    const QDateTime start = eveningStarts();
    const QDateTime end = curfewAt();
    if (m_now < start)
        return -1.0;
    const qint64 span = start.secsTo(end);
    return span > 0 ? std::clamp(static_cast<double>(start.secsTo(m_now)) / span, 0.0, 1.0) : 1.0;
}

QString CurfewViewModel::timeLeftText() const
{
    if (m_now < eveningStarts())
        return {};
    return fmt::countdown(m_now.secsTo(curfewAt()));
}

QString CurfewViewModel::nowText() const
{
    return fmt::clockBare(m_now.time());
}

QString CurfewViewModel::ruleText() const
{
    return lateNight() ? QStringLiteral("Friday–Saturday curfew") : QStringLiteral("Sunday–Thursday curfew");
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
