#include "httptransport.h"

#include "log.h"

#ifdef Q_OS_ANDROID
#  include <QJniEnvironment>
#  include <QJniObject>
#else
#  include <QEventLoop>
#  include <QNetworkAccessManager>
#  include <QNetworkReply>
#  include <QNetworkRequest>
#  include <QStringDecoder>
#endif

namespace mycu {

HttpTransport::HttpTransport(int timeoutMs, QString userAgent)
    : m_timeoutMs(timeoutMs)
    , m_userAgent(userAgent.isEmpty() ? USER_AGENT_SUFFIX : std::move(userAgent))
{}

#ifdef Q_OS_ANDROID

Response HttpTransport::get(const QString &path)
{
    const QString url = resolve(path);

    // HttpGet.get returns {status, finalUrl, contentType, body}; a status of -1
    // means the request never completed, and the body slot carries why. It
    // catches its own exceptions, so nothing is left pending on the JNI side.
    const QJniObject result = QJniObject::callStaticObjectMethod(
        "com/kromakobra/cedarview/HttpGet", "get",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)[Ljava/lang/String;",
        QJniObject::fromString(url).object<jstring>(),
        QJniObject::fromString(m_userAgent).object<jstring>(),
        QJniObject::fromString(QStringLiteral("application/json, */*")).object<jstring>(),
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
    if (status < 200 || status >= 300)
        throw TransportError(QStringLiteral("%1 returned HTTP %2").arg(url).arg(status));

    Response response;
    response.status = status;
    response.url = field(1).isEmpty() ? url : field(1);
    response.headers.insert(QStringLiteral("content-type"), field(2));
    response.body = field(3);
    return response;
}

#else

Response HttpTransport::get(const QString &path)
{
    const QString url = resolve(path);

    // Called on a pool thread, which has no event loop of its own — so this
    // one request gets a private manager and a local loop to wait in.
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent);
    request.setRawHeader("Accept", "application/json, */*");
    request.setTransferTimeout(m_timeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    reply->deleteLater();

    const QVariant statusAttr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!statusAttr.isValid()) {
        throw TransportError(QStringLiteral("could not reach %1: %2").arg(url, reply->errorString()));
    }
    const int status = statusAttr.toInt();
    if (status < 200 || status >= 300)
        throw TransportError(QStringLiteral("%1 returned HTTP %2").arg(url).arg(status));

    const QByteArray raw = reply->readAll();
    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();

    // Decode with the charset the server named, UTF-8 otherwise; undecodable
    // bytes are replaced rather than failing the request.
    QString body;
    const qsizetype at = contentType.indexOf(QStringLiteral("charset="), 0, Qt::CaseInsensitive);
    if (at >= 0) {
        const QString charset = contentType.mid(at + 8).section(u';', 0, 0).trimmed().remove(u'"');
        QStringDecoder decoder(charset.toLatin1().constData());
        if (decoder.isValid())
            body = decoder.decode(raw);
    }
    if (body.isEmpty())
        body = QString::fromUtf8(raw);

    Response response;
    response.status = status;
    response.url = reply->url().toString();
    response.body = body;
    response.headers.insert(QStringLiteral("content-type"), contentType);
    return response;
}

#endif

} // namespace mycu
