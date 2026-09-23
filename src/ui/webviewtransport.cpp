#include "webviewtransport.h"

#include "core/log.h"

#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QThread>
#include <QUuid>

#include <chrono>

namespace mycu {

const char *const FETCH_SCRIPT = R"JS(
(function () {
  window.__mycu = window.__mycu || {};
  window.__mycu[%1] = null;
  fetch(%2, {
    credentials: 'same-origin',
    redirect: 'follow',
    headers: %3
  })
    .then(function (r) {
      return r.text().then(function (t) {
        return {
          status: r.status,
          url: r.url,
          redirected: r.redirected,
          contentType: r.headers.get('content-type') || '',
          body: t
        };
      });
    })
    .catch(function (e) { return { error: String(e) }; })
    .then(function (r) { window.__mycu[%1] = JSON.stringify(r); });
})();
)JS";

const char *const READ_SCRIPT = R"JS(
(function () {
  if (!window.__mycu) { return ''; }
  var v = window.__mycu[%1];
  if (v) { delete window.__mycu[%1]; return v; }
  return '';
})();
)JS";

namespace {

// A JavaScript string literal for `text`.
QString jsString(const QString &text)
{
    const QByteArray array = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(array.mid(1, array.size() - 2));
}

QString jsObject(const QHash<QString, QString> &headers)
{
    QJsonObject obj;
    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
        obj.insert(it.key(), it.value());
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// A QML `var` may arrive as a QJSValue wrapped in a QVariant.
QVariant unwrap(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QJSValue>())
        return value.value<QJSValue>().toVariant();
    return value;
}

} // namespace

bool looksLikeCorsFailure(const QString &error)
{
    static const QStringList markers = {
        QStringLiteral("failed to fetch"), // Chromium / the Android system WebView
        QStringLiteral("networkerror"),    // Gecko
        QStringLiteral("load failed"),     // WebKit
        QStringLiteral("cors"),
    };
    const QString lowered = error.toLower();
    for (const QString &marker : markers) {
        if (lowered.contains(marker))
            return true;
    }
    return false;
}

WebViewTransport::WebViewTransport(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(POLL_INTERVAL_MS);
    connect(&m_timer, &QTimer::timeout, this, &WebViewTransport::poll);
}

WebViewTransport::~WebViewTransport()
{
    close();
}

void WebViewTransport::close()
{
    // Release anyone still waiting, rather than leaving a worker blocked until
    // its timeout on a surface that is never going to answer.
    std::lock_guard lock(m_mutex);
    for (const PendingPtr &pending : std::as_const(m_pending)) {
        if (!pending->done) {
            pending->error = QStringLiteral("the web transport was closed");
            pending->done = true;
        }
    }
    m_attached = false;
    m_cv.notify_all();
}

void WebViewTransport::attachSurface(QObject *surface)
{
    m_surface = surface;
    // `var` in QML is QVariant in C++; the string-based connect is the one
    // that can name a signal declared in QML.
    connect(surface, SIGNAL(evalResult(QString,QVariant)), this,
            SLOT(onEvalResult(QString,QVariant)));
    connect(surface, SIGNAL(currentUrlChanged()), this, SLOT(onSurfaceUrlChanged()));
    {
        std::lock_guard lock(m_mutex);
        m_attached = true;
        m_currentUrl = surface->property("currentUrl").toString();
    }
    qCInfo(lcWebView) << "transport attached to" << surface->metaObject()->className();
}

void WebViewTransport::onSurfaceUrlChanged()
{
    if (!m_surface)
        return;
    const QString url = m_surface->property("currentUrl").toString();
    std::lock_guard lock(m_mutex);
    m_currentUrl = url;
}

void WebViewTransport::setExtraHeaders(const QHash<QString, QString> &headers)
{
    m_extraHeaders = headers;
}

QString WebViewTransport::currentUrl() const
{
    std::lock_guard lock(m_mutex);
    return m_currentUrl;
}

void WebViewTransport::requireWorkerThread(const char *what) const
{
    if (QThread::currentThread() == thread()) {
        throw TransportError(QStringLiteral(
            "WebViewTransport::%1 was called on the GUI thread. It blocks waiting for that same "
            "thread to run JavaScript, so it would deadlock. Run providers through "
            "runInBackground().")
                                 .arg(QLatin1StringView(what)));
    }
}

WebViewTransport::PendingPtr WebViewTransport::enqueue(const QString &path, bool raw, int timeoutMs)
{
    auto pending = std::make_shared<Pending>();
    pending->token = QUuid::createUuid().toString(QUuid::Id128);
    pending->path = path;
    pending->raw = raw;
    pending->timeoutMs = timeoutMs;
    pending->started.start();
    std::lock_guard lock(m_mutex);
    m_pending.insert(pending->token, pending);
    return pending;
}

bool WebViewTransport::wait(const PendingPtr &pending, int timeoutMs)
{
    std::unique_lock lock(m_mutex);
    return m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs + 2000),
                         [&] { return pending->done; });
}

void WebViewTransport::forget(const QString &token)
{
    std::lock_guard lock(m_mutex);
    m_pending.remove(token);
}

Response WebViewTransport::get(const QString &path)
{
    return get(path, DEFAULT_TIMEOUT_MS);
}

Response WebViewTransport::get(const QString &path, int timeoutMs)
{
    {
        std::lock_guard lock(m_mutex);
        if (!m_attached)
            throw TransportError(QStringLiteral("no web surface attached; the UI is not ready yet"));
    }
    requireWorkerThread("get()");

    const QString url = resolve(path);

    // If the surface is already sitting on a sign-in page, the fetch would fail
    // on CORS and we would report a confusing transport error. Say the true
    // thing instead.
    const QString surfaceUrl = currentUrl();
    if (!surfaceUrl.isEmpty() && looksLikeLogin(surfaceUrl))
        throw SessionExpired(QStringLiteral("web surface is on a sign-in page (%1)").arg(surfaceUrl));

    const PendingPtr pending = enqueue(url, false, timeoutMs);
    const QString script = QString::fromUtf8(FETCH_SCRIPT)
                               .arg(jsString(pending->token), jsString(url), jsObject(m_extraHeaders));
    QMetaObject::invokeMethod(
        this, [this, token = pending->token, script] { startOnGui(token, script); },
        Qt::QueuedConnection);

    const bool finished = wait(pending, timeoutMs);
    forget(pending->token);
    if (!finished) {
        throw TransportError(QStringLiteral("timed out after %1s waiting for %2. The page may still be "
                                            "loading, or the WebView may have been destroyed.")
                                 .arg(timeoutMs / 1000)
                                 .arg(url));
    }

    QString error;
    QJsonObject payload;
    {
        std::lock_guard lock(m_mutex);
        error = pending->error;
        payload = pending->payload;
    }

    if (!error.isEmpty()) {
        // A fetch that fails outright — no status, no body — is very often not
        // a network fault but a sign-in in disguise: the page has been
        // redirected to the identity provider, and the browser refuses a
        // cross-origin request back to Self-Service. Chromium reports that as
        // a bare `TypeError: Failed to fetch`, which used to be passed straight
        // through as a transport error. The user got "Couldn't reach
        // Self-Service" and no way to sign in.
        //
        // currentUrl() is not trustworthy enough to settle it — on QtWebView it
        // can still hold the pre-redirect URL (see WebSurfaceAndroid.qml) — so
        // ask the page where it actually is. This costs one round trip and
        // only on the failure path.
        if (looksLikeCorsFailure(error)) {
            const QString where = pageLocation();
            if (!where.isEmpty() && looksLikeLogin(where)) {
                emit sessionExpired();
                throw SessionExpired(QStringLiteral("%1 could not be fetched because the browser is on "
                                                    "a sign-in page (%2); the session has ended")
                                         .arg(url, where));
            }
        }
        throw TransportError(QStringLiteral("fetch failed for %1: %2").arg(url, error));
    }

    Response response;
    response.status = payload.value(QStringLiteral("status")).toInt(0);
    const QString finalUrl = payload.value(QStringLiteral("url")).toString();
    response.url = finalUrl.isEmpty() ? url : finalUrl;
    response.body = payload.value(QStringLiteral("body")).toString();
    response.headers.insert(QStringLiteral("content-type"),
                            payload.value(QStringLiteral("contentType")).toString());

    if (looksLikeLogin(response.url, response.body)) {
        emit sessionExpired();
        throw SessionExpired(QStringLiteral("%1 redirected to %2; the Self-Service session has ended")
                                 .arg(url, response.url));
    }

    if (!response.ok())
        throw TransportError(QStringLiteral("%1 returned HTTP %2").arg(url).arg(response.status));

    qCDebug(lcWebView).noquote() << "fetched" << url << "->" << response.status << "("
                                 << response.body.size() << "chars)";
    return response;
}

QString WebViewTransport::pageLocation()
{
    try {
        return evaluate(QStringLiteral("window.location.href"), 5000).toString();
    } catch (const std::exception &e) {
        qCDebug(lcWebView) << "could not read the page location:" << e.what();
        return {};
    }
}

QVariant WebViewTransport::evaluate(const QString &script, int timeoutMs)
{
    {
        std::lock_guard lock(m_mutex);
        if (!m_attached)
            throw TransportError(QStringLiteral("no web surface attached"));
    }
    requireWorkerThread("evaluate()");

    const PendingPtr pending = enqueue(QStringLiteral("<evaluate>"), true, timeoutMs);
    QMetaObject::invokeMethod(
        this, [this, token = pending->token, script] { evalOnGui(token, script); },
        Qt::QueuedConnection);

    const bool finished = wait(pending, timeoutMs);
    forget(pending->token);
    if (!finished)
        throw TransportError(QStringLiteral("JavaScript evaluation timed out after %1s").arg(timeoutMs / 1000));

    std::lock_guard lock(m_mutex);
    if (!pending->error.isEmpty())
        throw TransportError(pending->error);
    return pending->value;
}

// ---------------------------------------------------------------------------
// GUI-thread internals
// ---------------------------------------------------------------------------

void WebViewTransport::evalOnGui(const QString &token, const QString &script)
{
    if (!m_surface) {
        fail(token, QStringLiteral("web surface disappeared"));
        return;
    }
    try {
        eval(token, script);
    } catch (const std::exception &e) {
        fail(token, QStringLiteral("could not evaluate script: %1").arg(QString::fromUtf8(e.what())));
    }
}

void WebViewTransport::startOnGui(const QString &token, const QString &script)
{
    if (!m_surface) {
        fail(token, QStringLiteral("web surface disappeared before the request started"));
        return;
    }
    try {
        eval(QString(), script); // fire and forget
    } catch (const std::exception &e) {
        fail(token, QStringLiteral("could not evaluate fetch script: %1").arg(QString::fromUtf8(e.what())));
        return;
    }
    if (!m_timer.isActive())
        m_timer.start();
}

void WebViewTransport::eval(const QString &token, const QString &script)
{
    // A QML-declared function is in the object's meta-object with QVariant
    // parameters, so it can be invoked by name.
    const bool ok = QMetaObject::invokeMethod(m_surface.data(), "evalAsync", Qt::DirectConnection,
                                              Q_ARG(QVariant, QVariant(token)),
                                              Q_ARG(QVariant, QVariant(script)));
    if (!ok)
        throw TransportError(QStringLiteral("the web surface has no evalAsync(token, script)"));
}

void WebViewTransport::poll()
{
    QList<PendingPtr> fetches;
    {
        std::lock_guard lock(m_mutex);
        // Bare evaluations answer through their own callback and must never be
        // polled with the fetch-result read script.
        for (const PendingPtr &pending : std::as_const(m_pending)) {
            if (!pending->raw && !pending->done)
                fetches.append(pending);
        }
    }

    if (fetches.isEmpty()) {
        m_timer.stop();
        return;
    }

    for (const PendingPtr &pending : std::as_const(fetches)) {
        if (pending->started.elapsed() > pending->timeoutMs) {
            fail(pending->token, QStringLiteral("no response within %1s").arg(pending->timeoutMs / 1000));
            continue;
        }
        try {
            eval(pending->token, QString::fromUtf8(READ_SCRIPT).arg(jsString(pending->token)));
        } catch (const std::exception &e) {
            fail(pending->token, QStringLiteral("poll failed: %1").arg(QString::fromUtf8(e.what())));
        }
    }
}

void WebViewTransport::onEvalResult(const QString &token, const QVariant &rawResult)
{
    // Empty for the fire-and-forget kickoff evaluation, which has no result
    // worth looking at.
    if (token.isEmpty())
        return;

    const QVariant result = unwrap(rawResult);

    bool isRaw = false;
    {
        std::lock_guard lock(m_mutex);
        const PendingPtr pending = m_pending.value(token);
        isRaw = pending && pending->raw;
    }

    if (isRaw) {
        // A bare evaluate(): the value is whatever the script returned, and
        // null/empty is a legitimate answer rather than "still pending".
        finish(token, {}, result, {});
        return;
    }

    const QString text = result.toString();
    if (text.isEmpty())
        return; // still pending

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        fail(token, QStringLiteral("malformed result payload: '%1'").arg(text.left(120)));
        return;
    }

    const QJsonObject payload = doc.object();
    const QJsonValue error = payload.value(QStringLiteral("error"));
    if (!error.isUndefined() && !error.isNull() && !error.toString().isEmpty()) {
        // A cross-origin redirect to the IdP surfaces here as a CORS TypeError.
        // That *is* an expired session, so say so rather than reporting an
        // opaque fetch failure.
        const QString message = error.toString();
        if (message.contains(QStringLiteral("Failed to fetch"))
            || message.contains(QStringLiteral("NetworkError"))) {
            finish(token,
                   QJsonObject{{QStringLiteral("status"), 0},
                               {QStringLiteral("url"), QString()},
                               {QStringLiteral("body"), QString()}},
                   {}, message + QStringLiteral(" (often means the session expired)"));
        } else {
            fail(token, message);
        }
        return;
    }

    finish(token, payload, {}, {});
}

void WebViewTransport::finish(const QString &token, const QJsonObject &payload, const QVariant &value,
                              const QString &error)
{
    std::lock_guard lock(m_mutex);
    const PendingPtr pending = m_pending.value(token);
    if (!pending || pending->done)
        return;
    pending->payload = payload;
    pending->value = value;
    pending->error = error;
    pending->done = true;
    m_cv.notify_all();
}

void WebViewTransport::fail(const QString &token, const QString &message)
{
    qCWarning(lcWebView).noquote() << "request" << token.left(8) << "failed:" << message;
    finish(token, {}, {}, message);
}

} // namespace mycu
