#include "android_sessiontransport.h"

#include "core/log.h"

#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

namespace mycu {

AndroidSessionTransport::AndroidSessionTransport(int timeoutMs)
    : m_timeoutMs(timeoutMs)
{}

Response AndroidSessionTransport::get(const QString &path)
{
    const QString url = resolve(path);

    // {status, finalUrl, contentType, body}; a status of -1 means the request
    // never completed, and the body slot carries why. The Java side catches its
    // own exceptions.
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const QJniObject result = QJniObject::callStaticObjectMethod(
        "com/kromakobra/cedarview/HttpGet", "getWithWebViewCookies",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;I)[Ljava/lang/String;",
        context.object(), QJniObject::fromString(url).object<jstring>(),
        QJniObject::fromString(QStringLiteral("application/json, text/html, */*")).object<jstring>(),
        jint(m_timeoutMs));

    QJniEnvironment env;
    if (env.checkAndClearExceptions() || !result.isValid())
        throw TransportError(QStringLiteral("could not reach %1: the Java HTTP helper failed").arg(url));

    const auto array = result.object<jobjectArray>();
    auto field = [&](int index) {
        jobject local = env->GetObjectArrayElement(array, index);
        const QString text = QJniObject(local).toString();
        env->DeleteLocalRef(local);
        return text;
    };

    const int status = field(0).toInt();
    if (status < 0)
        throw TransportError(QStringLiteral("could not reach %1: %2").arg(url, field(3)));

    Response response;
    response.status = status;
    response.url = field(1).isEmpty() ? url : field(1);
    response.headers.insert(QStringLiteral("content-type"), field(2));
    response.body = field(3);

    if (looksLikeLogin(response.url, response.body)) {
        throw SessionExpired(QStringLiteral("%1 redirected to %2; the Self-Service session has ended")
                                 .arg(url, response.url));
    }
    if (status >= 300 && status < 400)
        throw TransportError(QStringLiteral("%1 redirected off Self-Service, to %2").arg(url, response.url));
    if (!response.ok())
        throw TransportError(QStringLiteral("%1 returned HTTP %2").arg(url).arg(status));

    qCDebug(lcTransport).noquote() << "fetched" << url << "->" << status << "(" << response.body.size()
                                   << "chars)";
    return response;
}

} // namespace mycu
