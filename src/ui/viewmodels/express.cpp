#include "express.h"

#include "core/errors.h"
#include "core/log.h"
#include "format.h"
#include "ui/tasks.h"

#include <algorithm>

#include <QVariantMap>

namespace mycu {

namespace {

// Whether the failure is the network rather than the server.
bool isOffline(std::exception_ptr error)
{
    try {
        std::rethrow_exception(error);
    } catch (const TransportError &e) {
        return e.httpStatus() == 0;
    } catch (...) {
        return false;
    }
}

QString errorText(std::exception_ptr error)
{
    try {
        std::rethrow_exception(error);
    } catch (const ParseError &) {
        return QStringLiteral("The Commons Express menu changed shape.");
    } catch (const TransportError &e) {
        return QStringLiteral("Couldn't reach the Commons Express menu: ") + e.message();
    } catch (...) {
        return QStringLiteral("Unexpected error: ") + describe(error);
    }
}

} // namespace

ExpressViewModel::ExpressViewModel(TransportPtr transport, std::optional<Storage> storage, QObject *parent)
    : QObject(parent)
    , m_transport(std::move(transport))
    , m_storage(std::move(storage))
{
    fetchMenu = [this](ExpressProvider provider, Done done, Failed failed) {
        runInBackground(
            this,
            [provider]() mutable {
                const QJsonValue payload = provider.fetchPayload();
                return std::make_pair(parseExpress(payload), payload);
            },
            [done](const std::pair<ExpressMenu, QJsonValue> &result) { done(result.first, result.second); },
            failed);
    };
    m_status.now = [this] { return now(); };
    connect(&m_status, &SourceStatus::changed, this, &ExpressViewModel::changed);
    connect(this, &ExpressViewModel::changed, this, &ExpressViewModel::clockChanged);
    hydrate();
}

void ExpressViewModel::hydrate()
{
    if (!persisting())
        return;
    if (const auto saved = m_storage->cache.load(cachekey::EXPRESS)) {
        try {
            m_menu = parseExpress(saved->payload);
            m_status.restored(saved->savedAt);
        } catch (const std::exception &e) {
            qCWarning(lcDining) << "express: the saved menu is unreadable (ignored):" << e.what();
        }
    }
}

QVariantList ExpressViewModel::categories() const
{
    QVariantList out;
    for (const ExpressCategory &category : m_menu.categories) {
        QVariantList items;
        for (const ExpressItem &item : category.items) {
            items.append(QVariantMap{
                {QStringLiteral("name"), item.name},
                {QStringLiteral("price"), item.price},
                {QStringLiteral("description"), item.description},
            });
        }
        out.append(QVariantMap{{QStringLiteral("name"), category.name}, {QStringLiteral("items"), items}});
    }
    return out;
}

void ExpressViewModel::refresh()
{
    if (m_busy)
        return;
    m_busy = true;
    m_status.begin();

    const int generation = m_generation;
    fetchMenu(
        ExpressProvider(m_transport),
        [this, generation](const ExpressMenu &menu, const QJsonValue &payload) {
            if (generation != m_generation)
                return;
            if (persisting() && !payload.isUndefined() && !payload.isNull())
                m_storage->cache.save(cachekey::EXPRESS, payload, now());
            onLoaded(menu);
        },
        [this, generation](std::exception_ptr error) {
            if (generation == m_generation)
                onFailed(error);
        });
}

void ExpressViewModel::refreshAll()
{
    m_status.tick();
    emit clockChanged();
}

hours::Schedule ExpressViewModel::schedule() const
{
    if (!m_menu.hours.isEmpty())
        return [hours = m_menu.hours](QDate day) { return hours.on(day); };
    const auto &venues = hours::venues();
    const auto posted = std::find_if(venues.cbegin(), venues.cend(),
                                     [](const hours::Venue &v) { return v.name == ExpressProvider::label; });
    if (posted == venues.cend())
        return [](QDate) { return hours::DayWindows{}; };
    return [venue = *posted](QDate day) { return venue.on(day); };
}

bool ExpressViewModel::open() const
{
    return hours::isOpen(schedule(), now());
}

QString ExpressViewModel::hoursStatus() const
{
    const QDateTime at = now();
    const hours::Schedule hoursOf = schedule();
    if (const auto closes = hours::closesAt(hoursOf, at))
        return QStringLiteral("Open until ") + fmt::clock(closes->time());
    const auto next = hours::nextOpening(hoursOf, at);
    if (!next)
        return QString();
    const QDate day = next->opens.date();
    const QString when = day == at.date()           ? QString()
                       : day == at.date().addDays(1) ? QStringLiteral("tomorrow ")
                                                     : fmt::dayShort(day) + u' ';
    return QStringLiteral("Opens ") + when + fmt::clock(next->opens.time());
}

QVariantList ExpressViewModel::weekHours() const
{
    const QDate today = now().date();
    const QDate monday = today.addDays(1 - today.dayOfWeek());
    const hours::Schedule hoursOf = schedule();
    QVariantList rows;
    for (int i = 0; i < 7; ++i) {
        const QDate day = monday.addDays(i);
        QStringList spans;
        for (const hours::Window &w : hoursOf(day))
            spans.append(fmt::clockShort(hours::at(day, w.open).time()) + QStringLiteral("–")
                         + fmt::clockShort(hours::at(day, w.close).time()));
        rows.append(QVariantMap{
            {QStringLiteral("day"), fmt::dayShort(day)},
            {QStringLiteral("hours"), spans.isEmpty() ? QStringLiteral("Closed") : spans.join(QStringLiteral(", "))},
            {QStringLiteral("today"), day == today},
        });
    }
    return rows;
}

void ExpressViewModel::onLoaded(const ExpressMenu &menu)
{
    m_busy = false;
    const bool same = menu == m_menu;
    m_menu = menu;
    m_status.succeeded(now());
    qCInfo(lcDining) << "express:" << menu.categories.size() << "categories loaded"
                     << (menu.hours.isEmpty() ? "(posted hours)" : "") << (same ? "(unchanged)" : "");
    if (!same)
        emit changed();
}

void ExpressViewModel::onFailed(std::exception_ptr error)
{
    m_busy = false;
    m_status.failed(errorText(error), isOffline(error));
    qCCritical(lcDining).noquote() << "express refresh failed:" << describe(error);
}

void ExpressViewModel::setPreview(bool on)
{
    if (on == m_preview)
        return;
    m_preview = on;
    ++m_generation;
    m_busy = false;
    m_menu = ExpressMenu();
    m_status.reset();
    hydrate();
    emit changed();
}

} // namespace mycu
