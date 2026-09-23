#include "meals.h"

#include "../htmlattrs.h"
#include "../log.h"
#include "../pyjson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace mycu {

namespace {

// Which cycle the meal count runs on, read off `PlanName`. Cedarville's weekly
// plans are named for their count ("21 Meals", "14 Meals"); the per-term plan
// is "Block 120". Block is checked first because its name has a number in it
// too. An unrecognised name gives "", which the UI renders as no qualifier at
// all rather than a guessed one.
QString periodOf(const QString &planName)
{
    static const QRegularExpression term(QStringLiteral("\\bblock\\b"),
                                         QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression week(QStringLiteral("^\\s*\\d+\\s+meals?\\b"),
                                         QRegularExpression::CaseInsensitiveOption);
    if (term.match(planName).hasMatch())
        return QStringLiteral("term");
    if (week.match(planName).hasMatch())
        return QStringLiteral("week");
    return {};
}

// "", or the value as text when it is truthy — the `str(x or "")` idiom.
QString textOr(const QJsonValue &value)
{
    return json::truthy(value) ? json::str(value) : QString();
}

// `RecentTransactions` -> newest-first MealTransaction list.
//
// Tolerant on purpose: the history is secondary to the balances, so a missing
// list is no history and a malformed row is dropped, never a ParseError.
// Undated rows sort last.
QList<MealTransaction> transactionsFrom(const QJsonValue &rows)
{
    if (!rows.isArray())
        return {};

    QList<MealTransaction> out;
    for (const QJsonValue &rowValue : rows.toArray()) {
        if (!rowValue.isObject())
            continue;
        const QJsonObject row = rowValue.toObject();
        MealTransaction t;
        t.at = json::parseIsoDateTime(row.value(QStringLiteral("Date")));
        t.activity = textOr(row.value(QStringLiteral("Activity"))).trimmed();
        t.mealPeriod = textOr(row.value(QStringLiteral("MealPeriod"))).trimmed();
        t.amount = json::asNumber(row.value(QStringLiteral("Amount")));
        t.isDeposit = json::truthy(row.value(QStringLiteral("IsDeposit")));
        out.append(t);
    }

    std::stable_sort(out.begin(), out.end(), [](const MealTransaction &a, const MealTransaction &b) {
        const bool aDated = a.at.isValid();
        const bool bDated = b.at.isValid();
        if (aDated != bDated)
            return aDated;
        if (!aDated)
            return false;
        return a.at > b.at;
    });
    return out;
}

// quote_plus, as the page's own query string would be built.
QString formEncode(const QString &value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value).replace("%20", "+"));
}

} // namespace

MealsProvider::MealsProvider(TransportPtr transport)
    : m_transport(std::move(transport))
{}

MealPlan MealsProvider::fetch()
{
    const Response page = m_transport->get(path);
    page.raiseForSession();
    const MealsTarget target = parseTarget(page.body);
    const Response response = m_transport->get(balancePath(target));
    return parse(response.raiseForSession());
}

MealPlan MealsProvider::parse(const Response &response)
{
    return parseBalance(response.body);
}

MealsTarget parseTarget(const QString &body)
{
    if (body.trimmed().isEmpty())
        throw ParseError(QStringLiteral("empty response body for the meal-plan page"));

    if (const auto attrs = html::firstElementWith(body, QStringLiteral("data-target-id"))) {
        return MealsTarget{attrs->value(QStringLiteral("data-target-id")).trimmed(),
                           attrs->value(QStringLiteral("data-target-card")).trimmed()};
    }

    throw ParseError(QStringLiteral(
        "the meal-plan page has no data-target-id — it has changed shape again; recapture with "
        "`scripts/discover meals`"));
}

QString balancePath(const MealsTarget &target)
{
    QStringList params;
    if (!target.personId.isEmpty())
        params.append(QStringLiteral("id=") + formEncode(target.personId));
    if (!target.card.isEmpty())
        params.append(QStringLiteral("card=") + formEncode(target.card));
    return params.isEmpty() ? BALANCE_PATH : BALANCE_PATH + u'?' + params.join(u'&');
}

MealPlan parseBalance(const QString &body)
{
    if (body.trimmed().isEmpty())
        throw ParseError(QStringLiteral("empty response body for the meal-plan balances"));

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
        throw ParseError(QStringLiteral("meal-plan balances are not JSON: %1").arg(error.errorString()));
    if (!doc.isObject())
        throw ParseError(QStringLiteral("meal-plan balances are not a JSON object"));
    const QJsonObject data = doc.object();

    const QJsonValue statusValue = data.value(QStringLiteral("Status"));
    const QString status = textOr(statusValue).toLower();
    if (status == u"error") {
        throw ParseError(QStringLiteral("Self-Service could not load the meal plan: %1")
                             .arg(json::str(data.value(QStringLiteral("Message")))));
    }
    if (status == u"no_card" || (status == u"ok" && !json::truthy(data.value(QStringLiteral("Found"))))) {
        qCInfo(lcMeals).noquote() << "meals: no plan on file (" + status + ")";
        return {};
    }
    if (status != u"ok")
        throw ParseError(QStringLiteral("unrecognised meal-plan status %1").arg(json::repr(statusValue)));

    const QJsonValue balances = data.value(QStringLiteral("Balances"));
    if (!balances.isArray())
        throw ParseError(QStringLiteral("meal-plan response has no Balances list — it has changed shape"));

    MealPlan plan;
    plan.planName = textOr(data.value(QStringLiteral("PlanName"))).trimmed();
    for (const QJsonValue &tenderValue : balances.toArray()) {
        if (!tenderValue.isObject())
            continue;
        const QJsonObject tender = tenderValue.toObject();
        const QString name = textOr(tender.value(QStringLiteral("Name")));
        const QString kind = textOr(tender.value(QStringLiteral("Type"))).toUpper();
        const std::optional<double> amount = json::asNumber(tender.value(QStringLiteral("Amount")));
        if (!amount)
            continue;

        if (kind == u"MEAL" && !plan.mealsRemaining) {
            plan.mealsRemaining = static_cast<int>(*amount);
        } else if (json::truthy(tender.value(QStringLiteral("IsCurrency"))) || kind == u"DEBIT") {
            const QString lowered = name.toLower();
            const bool voluntary = std::any_of(VOLUNTARY_WORDS.cbegin(), VOLUNTARY_WORDS.cend(),
                                               [&](const QString &w) { return lowered.contains(w); });
            if (voluntary) {
                if (!plan.flexDollars)
                    plan.flexDollars = *amount;
            } else if (!plan.diningDollars) {
                plan.diningDollars = *amount;
            }
        }
    }
    plan.period = periodOf(plan.planName);
    plan.transactions = transactionsFrom(data.value(QStringLiteral("RecentTransactions")));

    if (!balances.toArray().isEmpty() && !plan.hasAny()) {
        QStringList names;
        for (const QJsonValue &tender : balances.toArray()) {
            if (tender.isObject())
                names.append(json::repr(tender.toObject().value(QStringLiteral("Name"))));
        }
        throw ParseError(QStringLiteral("no recognisable meal-plan balances among: %1")
                             .arg(names.join(QStringLiteral(", "))));
    }

    auto shown = [](auto value) {
        return value ? QString::number(*value) : QStringLiteral("None");
    };
    qCDebug(lcMeals).noquote() << QStringLiteral("meals: %1 meals, dining=%2, flex=%3, plan='%4', %5 transactions")
                                      .arg(shown(plan.mealsRemaining), shown(plan.diningDollars),
                                           shown(plan.flexDollars), plan.planName)
                                      .arg(plan.transactions.size());
    return plan;
}

} // namespace mycu
