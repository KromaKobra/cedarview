// The WebView transport: make the browser fetch it, and take the body back.
//
// Why this exists
// ---------------
//
// We need authenticated requests to `selfservice.cedarville.edu`. The session
// lives in an `HttpOnly` ASP.NET cookie. On Android, Qt cannot read it — the
// `cookieAdded` signal fires only for cookies *we* set — so the obvious design
// (harvest the cookie, drive an HTTP client) is simply unavailable.
//
// The way around it is to not need the cookie. The WebView already has it and
// attaches it automatically. So we evaluate a `fetch()` **inside the logged-in
// page**, let the browser do the request with its own credentials, and read the
// body back out. The only things involved are the page URL and
// `runJavaScript`, both present on QtWebEngine *and* QtWebView. That is what
// makes the Android port wiring rather than research.
//
// Why it polls
// ------------
//
// `runJavaScript` returns the value of the last expression; it cannot await a
// promise, and `fetch` is a promise. So the script is fire-and-forget: it parks
// its result on `window.__mycu[token]`, and we poll that slot with cheap
// follow-up evaluations until it is populated. Verbose, but it is the shape
// that was verified working (287 KB of body came back intact), and it is
// identical on both backends.
//
// The `.catch` in the script is load-bearing. An unauthenticated fetch
// redirects cross-origin to Microsoft and throws a CORS `TypeError`; without
// the catch the result slot is never written and the request hangs until
// timeout. That is exactly what the first run of this experiment did.
//
// Threading
// ---------
//
// get() is **synchronous and must be called from a worker thread**. Providers
// stay simple and testable that way. The JavaScript is posted to the GUI thread
// with a queued call, and the calling thread blocks on a condition variable
// until the GUI thread deposits a result. Calling get() from the GUI thread
// would deadlock — it throws TransportError instead of hanging.

#pragma once

#include "core/transport.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariant>

#include <condition_variable>
#include <memory>
#include <mutex>

namespace mycu {

// How often the GUI thread checks whether the in-page fetch has finished.
// 60 ms is well under human perception and costs a trivial JS evaluation.
inline constexpr int POLL_INTERVAL_MS = 60;

// How long a single request may take before we give up. Generous, because the
// first request after a cold start can involve a full SAML round trip.
inline constexpr int DEFAULT_TIMEOUT_MS = 30000;

// How a browser reports a request it refused to make, as opposed to one that
// came back with an error status. Chromium says "TypeError: Failed to fetch"
// for a CORS refusal; other engines word it differently, hence the set. A
// request that was *refused* is the signature of the page having been
// redirected off our origin — i.e. of a sign-in — so it gets a second look.
bool looksLikeCorsFailure(const QString &error);

// The fetch, in the page's own origin. %1 is the token, %2 the URL and %3 the
// extra headers, each already JSON-encoded.
//
// `credentials: 'same-origin'` is what makes the browser attach the session
// cookie. `redirect: 'follow'` is the default and is what lets `r.url` reveal
// that we ended up at Microsoft — which is how expiry is detected.
extern const char *const FETCH_SCRIPT;

// Reads and consumes the result slot. Returns '' while still pending, so the
// C++ side can test emptiness without worrying about null/undefined
// marshalling differences between the two backends.
extern const char *const READ_SCRIPT;

// A Transport backed by the WebView.
//
// Construct it on the GUI thread, hand it the QML web surface with
// attachSurface(), then call get() from worker threads.
class WebViewTransport : public QObject, public Transport
{
    Q_OBJECT

public:
    explicit WebViewTransport(QObject *parent = nullptr);
    ~WebViewTransport() override;

    // Bind to the QML web surface.
    //
    // `surface` is whichever WebSurface*.qml got loaded. They all expose the
    // same members, and this class uses nothing else:
    //
    //   evalAsync(token, script)    run JavaScript; deliver via evalResult
    //   evalResult(token, result)   signal carrying the value back
    //   currentUrl                  the page the surface is showing right now
    void attachSurface(QObject *surface);

    // Add request headers to every fetch.
    //
    // Empty by default, on purpose. It is tempting to send
    // `X-Requested-With: XMLHttpRequest` because ASP.NET MVC uses it to decide
    // between a full page and a partial/JSON response — but that *changes what
    // comes back*. Set it deliberately once you know you want it.
    void setExtraHeaders(const QHash<QString, QString> &headers);

    // Fail every request in flight and refuse new ones. Called at shutdown,
    // once the surface is gone, so no worker is left waiting for an answer
    // that cannot come.
    void close();

    // The surface's current URL. Safe from any thread.
    QString currentUrl() const;

    // Fetch `path` through the WebView. Blocks; call from a worker thread.
    //
    // Throws SessionExpired if the request lands on an identity provider, and
    // TransportError for everything else.
    Response get(const QString &path) override;
    Response get(const QString &path, int timeoutMs);

    // Run JavaScript in the current page and return its value.
    //
    // The value of the script's **last expression**, marshalled back through
    // Qt — so strings, numbers, booleans and (on QtWebEngine) plain objects
    // work, but a promise does not. That limitation is the whole reason get()
    // polls instead of awaiting.
    //
    // Blocks, so like get() it must be called from a worker thread.
    QVariant evaluate(const QString &script, int timeoutMs = 15000);

signals:
    // Emitted when a request lands on an identity provider. The app uses this
    // to raise the login surface without waiting for the worker thread to
    // propagate the exception.
    void sessionExpired();

private slots:
    void onEvalResult(const QString &token, const QVariant &result);
    void onSurfaceUrlChanged();

private:
    struct Pending
    {
        QString token;
        QString path;
        bool raw = false; // an evaluate(), not a fetch
        bool done = false;
        QJsonObject payload;
        QVariant value;
        QString error;
        QElapsedTimer started;
        int timeoutMs = DEFAULT_TIMEOUT_MS;
    };
    using PendingPtr = std::shared_ptr<Pending>;

    void requireWorkerThread(const char *what) const;
    PendingPtr enqueue(const QString &path, bool raw, int timeoutMs);
    bool wait(const PendingPtr &pending, int timeoutMs);
    void forget(const QString &token);

    // GUI thread only.
    void startOnGui(const QString &token, const QString &script);
    void evalOnGui(const QString &token, const QString &script);
    void eval(const QString &token, const QString &script);
    void poll();
    void finish(const QString &token, const QJsonObject &payload, const QVariant &value,
                const QString &error);
    void fail(const QString &token, const QString &message);

    // Where the browser actually is, asked of the page itself. Never throws.
    QString pageLocation();

    QPointer<QObject> m_surface;
    QTimer m_timer;
    QHash<QString, QString> m_extraHeaders;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    QHash<QString, PendingPtr> m_pending;
    bool m_attached = false;
    QString m_currentUrl;
};

} // namespace mycu
