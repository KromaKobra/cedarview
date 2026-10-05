#include "sourcestatus.h"

#include "format.h"

#include <algorithm>

namespace mycu {

SourceStatus::SourceStatus(qint64 freshSecs, QObject *parent)
    : QObject(parent)
    , m_freshSecs(freshSecs)
{}

qint64 SourceStatus::ageSecs() const
{
    if (!m_hasData || !m_updatedAt.isValid())
        return -1;
    return std::max<qint64>(0, m_updatedAt.secsTo(now()));
}

bool SourceStatus::stale() const
{
    return m_hasData && ageSecs() > m_freshSecs;
}

QString SourceStatus::describeAge(const QDateTime &at, const QDateTime &now)
{
    if (!at.isValid())
        return {};
    const QDate day = at.date();
    if (day == now.date())
        return QStringLiteral("Updated ") + fmt::clock(at.time());
    if (day == now.date().addDays(-1))
        return QStringLiteral("Updated yesterday");
    return QStringLiteral("Updated ") + fmt::monthDay(day);
}

QString SourceStatus::updatedText() const
{
    return m_hasData ? describeAge(m_updatedAt, now()) : QString();
}

QString SourceStatus::stampText() const
{
    const QString text = updatedText();
    return text.startsWith(QStringLiteral("Updated ")) ? text.mid(8) : text;
}

void SourceStatus::begin()
{
    m_loading = true;
    emit changed();
}

void SourceStatus::succeeded(const QDateTime &at)
{
    m_hasData = true;
    m_loading = false;
    m_fromCache = false;
    m_offline = false;
    m_needsSignIn = false;
    m_updatedAt = at;
    m_error.clear();
    emit changed();
    emit loaded();
}

void SourceStatus::restored(const QDateTime &savedAt)
{
    m_hasData = true;
    m_fromCache = true;
    m_updatedAt = savedAt;
    emit changed();
}

void SourceStatus::failed(const QString &message, bool offline)
{
    m_loading = false;
    m_error = message;
    m_offline = offline;
    emit changed();
}

void SourceStatus::stopped()
{
    m_loading = false;
    emit changed();
}

void SourceStatus::reset()
{
    m_hasData = false;
    m_loading = false;
    m_fromCache = false;
    m_offline = false;
    m_updatedAt = QDateTime();
    m_error.clear();
    emit changed();
}

void SourceStatus::setNeedsSignIn(bool value)
{
    if (m_needsSignIn == value)
        return;
    m_needsSignIn = value;
    emit changed();
}

void SourceStatus::setAwaitingSignIn(bool value)
{
    if (m_awaitingSignIn == value)
        return;
    m_awaitingSignIn = value;
    emit changed();
}

void SourceStatus::tick()
{
    if (m_hasData)
        emit changed();
}

} // namespace mycu
