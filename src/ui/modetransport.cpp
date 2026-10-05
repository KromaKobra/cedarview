#include "modetransport.h"

#include "core/providers/chapel_schedule.h"
#include "demotransport.h"

namespace mycu {

ModeTransport::ModeTransport(TransportPtr live, TransportPtr preview)
    : m_live(std::move(live))
    , m_sample(std::move(preview))
{}

Response ModeTransport::get(const QString &path)
{
    return (m_preview.load() ? m_sample : m_live)->get(path);
}

TransportPtr makePreviewTransport(const QString &root, std::function<QDate()> today)
{
    auto fixtures = std::make_shared<FixtureTransport>(root);
    auto router = std::make_shared<TransportRouter>(std::make_shared<RedatedBalanceTransport>(fixtures, today));
    router->route(DINING_BASE, std::make_shared<RedatedMenusTransport>(fixtures, today))
        .route(CHAPEL_MEDIA_BASE, std::make_shared<RedatedScheduleTransport>(fixtures, today));
    return router;
}

Response UnavailableTransport::get(const QString &path)
{
    throw TransportError(QStringLiteral("%1 is not available with --demo: there is no network or sign-in")
                             .arg(path));
}

} // namespace mycu
