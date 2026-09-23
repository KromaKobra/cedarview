#include "semester.h"

#include "core/calendar.h"
#include "core/log.h"
#include "format.h"

namespace mycu {

SemesterViewModel::SemesterViewModel(QObject *parent)
    : QObject(parent)
    , m_today(QDate::currentDate())
{}

bool SemesterViewModel::inTerm() const
{
    return currentTerm(m_today).has_value();
}

QString SemesterViewModel::termName() const
{
    const auto term = currentTerm(m_today);
    return term ? term->name : QString();
}

int SemesterViewModel::daysLeft() const
{
    const auto term = currentTerm(m_today);
    return term ? term->daysLeft(m_today) : -1;
}

int SemesterViewModel::totalDays() const
{
    const auto term = currentTerm(m_today);
    return term ? term->totalDays(m_today.year()) : -1;
}

double SemesterViewModel::elapsedFraction() const
{
    const auto term = currentTerm(m_today);
    return term ? term->elapsedFraction(m_today) : 0.0;
}

QString SemesterViewModel::endDateText() const
{
    const auto term = currentTerm(m_today);
    if (!term)
        return {};
    return QStringLiteral("ends ") + fmt::shortDate(term->endIn(m_today.year()));
}

QString SemesterViewModel::nextTermText() const
{
    const auto upcoming = nextTermStart(m_today);
    if (!upcoming)
        return {};
    return upcoming->first.name + QStringLiteral(" starts ") + fmt::shortDate(upcoming->second);
}

void SemesterViewModel::refreshAll()
{
    const QDate now = today();
    if (now == m_today)
        return;
    m_today = now;
    qCInfo(lcSemester).noquote() << "semester: date rolled over to" << now.toString(Qt::ISODate);
    emit changed();
}

} // namespace mycu
