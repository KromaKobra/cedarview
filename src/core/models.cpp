#include "models.h"

#include <QLocale>

#include <algorithm>
#include <cmath>

namespace mycu {

namespace {

// "anytime" -> "Anytime", "late night" -> "Late Night".
QString titleCase(const QString &text)
{
    QString out = text.toLower();
    bool start = true;
    for (QChar &c : out) {
        if (c.isLetter()) {
            if (start)
                c = c.toUpper();
            start = false;
        } else {
            start = true;
        }
    }
    return out;
}

} // namespace

QString ChapelLedgerEntry::when() const
{
    if (on.isValid()) {
        // "Thu Aug 20, 2026". C-locale names, whatever the phone's language.
        return QLocale::c().toString(on, QStringLiteral("ddd MMM")) + u' '
            + QString::number(on.date().day()) + QStringLiteral(", ")
            + QString::number(on.date().year());
    }
    if (!reason.isEmpty())
        return reason;
    if (!entryType.isEmpty())
        return entryType;
    return QStringLiteral("—");
}

int MenuBlock::sortKey() const
{
    const qsizetype index = SLOT_ORDER.indexOf(slot.toCaseFolded());
    return index >= 0 ? static_cast<int>(index) : static_cast<int>(SLOT_ORDER.size());
}

QString MenuBlock::heading() const
{
    if (!meal.isEmpty())
        return meal;
    return SLOT_LABELS.value(slot.toCaseFolded(), titleCase(slot));
}

QList<MenuBlock> DayMenu::forVenue(const QString &venue) const
{
    const QString wanted = venue.toCaseFolded();
    QList<MenuBlock> matching;
    for (const MenuBlock &block : blocks) {
        if (block.venue.toCaseFolded() == wanted)
            matching.append(block);
    }
    std::stable_sort(matching.begin(), matching.end(),
                     [](const MenuBlock &a, const MenuBlock &b) { return a.sortKey() < b.sortKey(); });
    return matching;
}

QStringList DayMenu::venues() const
{
    QStringList seen;
    for (const MenuBlock &block : blocks) {
        if (!seen.contains(block.venue))
            seen.append(block.venue);
    }
    return seen;
}

QString UpcomingChapel::who() const
{
    if (!speakers.isEmpty())
        return speakers.join(QStringLiteral(", "));
    return !title.isEmpty() ? title : QStringLiteral("Chapel");
}

bool UpcomingChapel::isSameAsTitle() const
{
    return title.trimmed().toCaseFolded() == who().trimmed().toCaseFolded();
}

bool MealTransaction::isFlex() const
{
    return amount.has_value() || activity.toLower().contains(QStringLiteral("flex"));
}

QString MealTransaction::amountText() const
{
    if (!amount)
        return {};
    const QString sign = isDeposit ? QStringLiteral("+") : QStringLiteral("−");
    return sign + MealPlan::money(std::fabs(*amount));
}

QString MealPlan::periodText() const
{
    return period.isEmpty() ? QString() : QStringLiteral("this ") + period;
}

QString MealPlan::planDescription() const
{
    if (!planName.isEmpty())
        return period == u"week" ? planName + QStringLiteral(" per week") : planName;
    if (period == u"week")
        return QStringLiteral("Weekly meal plan");
    if (period == u"term")
        return QStringLiteral("Semester meal plan");
    return {};
}

bool MealPlan::hasAny() const
{
    return mealsRemaining.has_value() || diningDollars.has_value() || flexDollars.has_value();
}

QString MealPlan::money(std::optional<double> value)
{
    if (!value)
        return {};

    // "$1,234.50": two places, thousands separated by commas, whatever the
    // phone's locale — this is dollars, and it should read like dollars.
    QString digits = QString::number(std::fabs(*value), 'f', 2);
    const bool negative = *value < 0 && digits != u"0.00";
    qsizetype point = digits.indexOf(u'.');
    for (qsizetype i = point - 3; i > 0; i -= 3)
        digits.insert(i, u',');
    return QStringLiteral("$") + (negative ? QStringLiteral("-") : QString()) + digits;
}

} // namespace mycu
