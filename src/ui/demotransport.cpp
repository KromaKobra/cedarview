#include "demotransport.h"

#include "core/providers/dining.h"
#include "core/providers/meals.h"
#include "core/pyjson.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace mycu {

namespace {

// The live API's cap; see providers/dining.h.
constexpr int MAX_DAYS = 31;

QDate todayFrom(const TodayFn &today)
{
    return today ? today() : QDate::currentDate();
}

QString compact(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace

RedatedMenusTransport::RedatedMenusTransport(TransportPtr fixtures, TodayFn today)
    : m_fixtures(std::move(fixtures))
    , m_today(std::move(today))
{}

Response RedatedMenusTransport::get(const QString &path)
{
    Response response = m_fixtures->get(path);

    const QJsonObject captured = response.json().toObject();
    QStringList sources = captured.keys();
    sources.sort();
    if (sources.isEmpty())
        return response;

    const QUrlQuery query(QUrl(resolve(path)));
    bool ok = false;
    int days = query.queryItemValue(QStringLiteral("days")).toInt(&ok);
    days = ok ? std::clamp(days, 1, MAX_DAYS) : DEFAULT_DAYS;
    QDate start = QDate::fromString(query.queryItemValue(QStringLiteral("start")), Qt::ISODate);
    if (!start.isValid())
        start = todayFrom(m_today);

    QJsonObject redated;
    for (int i = 0; i < days; ++i) {
        const QDate on = start.addDays(i);
        const QString &source = sources.at(on.toJulianDay() % sources.size());
        redated.insert(on.toString(Qt::ISODate), captured.value(source));
    }
    response.body = compact(redated);
    return response;
}

RedatedScheduleTransport::RedatedScheduleTransport(TransportPtr fixtures, TodayFn today)
    : m_fixtures(std::move(fixtures))
    , m_today(std::move(today))
{}

Response RedatedScheduleTransport::get(const QString &path)
{
    Response response = m_fixtures->get(path);
    QJsonObject envelope = response.json().toObject();
    QJsonArray items = envelope.value(QStringLiteral("Items")).toArray();
    if (items.isEmpty())
        return response;

    // The chapel to land on today: the first one on today's weekday, or on a
    // Monday when today is the weekend (and it lands on the coming Monday).
    QDate target = todayFrom(m_today);
    if (target.dayOfWeek() > 5)
        target = target.addDays(8 - target.dayOfWeek());
    qint64 shift = 0;
    for (const QJsonValue &item : items) {
        const QDateTime at = json::parseIsoDateTime(item.toObject().value(QStringLiteral("Date")));
        if (at.isValid() && at.toUTC().date().dayOfWeek() == target.dayOfWeek()) {
            shift = at.toUTC().date().daysTo(target);
            break;
        }
    }

    for (qsizetype i = 0; i < items.size(); ++i) {
        QJsonObject item = items.at(i).toObject();
        const QDateTime at = json::parseIsoDateTime(item.value(QStringLiteral("Date")));
        if (at.isValid()) {
            item.insert(QStringLiteral("Date"),
                        at.toUTC().addDays(shift).toString(Qt::ISODate));
        }
        items.replace(i, item);
    }
    envelope.insert(QStringLiteral("Items"), items);
    response.body = compact(envelope);
    return response;
}

RedatedBalanceTransport::RedatedBalanceTransport(TransportPtr fixtures, TodayFn today)
    : m_fixtures(std::move(fixtures))
    , m_today(std::move(today))
{}

Response RedatedBalanceTransport::get(const QString &path)
{
    Response response = m_fixtures->get(path);
    if (!QUrl(resolve(path)).path().startsWith(BALANCE_PATH, Qt::CaseInsensitive))
        return response;

    QJsonObject balance = response.json().toObject();
    QJsonArray rows = balance.value(QStringLiteral("RecentTransactions")).toArray();
    QDate newest;
    for (const QJsonValue &row : rows) {
        const QDateTime at = json::parseIsoDateTime(row.toObject().value(QStringLiteral("Date")));
        if (at.isValid() && (!newest.isValid() || at.date() > newest))
            newest = at.date();
    }
    if (!newest.isValid())
        return response;

    const qint64 shift = newest.daysTo(todayFrom(m_today).addDays(-1));
    for (qsizetype i = 0; i < rows.size(); ++i) {
        QJsonObject row = rows.at(i).toObject();
        const QDateTime at = json::parseIsoDateTime(row.value(QStringLiteral("Date")));
        if (at.isValid())
            row.insert(QStringLiteral("Date"), at.addDays(shift).toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")));
        rows.replace(i, row);
    }
    balance.insert(QStringLiteral("RecentTransactions"), rows);
    response.body = compact(balance);
    return response;
}

} // namespace mycu
