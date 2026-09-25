#include "demotransport.h"

#include "core/providers/dining.h"

#include <QDate>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace mycu {

namespace {

// The live API's cap; see providers/dining.h.
constexpr int MAX_DAYS = 31;

} // namespace

RedatedMenusTransport::RedatedMenusTransport(TransportPtr fixtures)
    : m_fixtures(std::move(fixtures))
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
        start = QDate::currentDate();

    QJsonObject redated;
    for (int i = 0; i < days; ++i) {
        const QDate on = start.addDays(i);
        const QString &source = sources.at(on.toJulianDay() % sources.size());
        redated.insert(on.toString(Qt::ISODate), captured.value(source));
    }
    response.body = QString::fromUtf8(QJsonDocument(redated).toJson(QJsonDocument::Compact));
    return response;
}

} // namespace mycu
