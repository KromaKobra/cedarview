#include "calendar.h"

#include <algorithm>

namespace mycu {

namespace {

// In calendar order within a year, which is what nextTermStart() walks.
const TermWindow *const TERMS[] = {&SPRING, &FALL};

} // namespace

bool TermWindow::contains(QDate day) const
{
    return startIn(day.year()) <= day && day <= endIn(day.year());
}

int TermWindow::daysLeft(QDate day) const
{
    return std::max<qint64>(0, day.daysTo(endIn(day.year())));
}

int TermWindow::totalDays(int year) const
{
    return static_cast<int>(startIn(year).daysTo(endIn(year)));
}

double TermWindow::elapsedFraction(QDate day) const
{
    const int total = totalDays(day.year());
    if (total <= 0)
        return 0.0;
    return std::clamp(1.0 - static_cast<double>(daysLeft(day)) / total, 0.0, 1.0);
}

std::optional<TermWindow> currentTerm(QDate day)
{
    for (const TermWindow *term : TERMS) {
        if (term->contains(day))
            return *term;
    }
    return std::nullopt;
}

std::optional<std::pair<TermWindow, QDate>> nextTermStart(QDate day)
{
    if (currentTerm(day))
        return std::nullopt;

    // This year's remaining starts first, then January of next year. Two years
    // is always enough: the gap between any two term starts is under twelve
    // months, so the first hit is never further out than next spring.
    for (int year : {day.year(), day.year() + 1}) {
        for (const TermWindow *term : TERMS) {
            const QDate starts = term->startIn(year);
            if (starts > day)
                return std::make_pair(*term, starts);
        }
    }
    return std::nullopt; // unreachable while TERMS is non-empty
}

} // namespace mycu
