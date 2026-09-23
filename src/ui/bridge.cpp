#include "bridge.h"

#include "core/log.h"
#include "webviewtransport.h"

#include <QCoreApplication>

namespace mycu {

Bridge::Bridge(WebViewTransport *transport, QString startPath, QString platformName,
               QString surfaceQml, QObject *parent)
    : QObject(parent)
    , m_transport(transport)
    , m_startPath(std::move(startPath))
    , m_platformName(std::move(platformName))
    , m_surfaceQml(std::move(surfaceQml))
{}

QString Bridge::version() const
{
    return QCoreApplication::applicationVersion();
}

void Bridge::attachSurface(QObject *surface)
{
    if (!m_transport) {
        qCInfo(lcApp) << "demo mode: web surface ignored";
        return;
    }
    m_transport->attachSurface(surface);
}

} // namespace mycu
