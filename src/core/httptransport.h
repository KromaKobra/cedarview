// A plain HTTPS client, for origins that need no session.
//
// Used only for **unauthenticated** endpoints — the dining menu service, whose
// own page fetches it with `credentials: "omit"`, and the chapel media API.
//
// Why this exists alongside the WebView transport: the WebView is needed only
// because the Self-Service session cookie is unreachable. Where there is no
// cookie to worry about, routing through a browser would be slower, harder to
// debug, and would need the surface parked on that origin to satisfy CORS. A
// direct request is simply the right tool. Never send credentials through this
// class; that is what WebViewTransport is for.
//
// Two implementations behind one class:
//
// * **Desktop** — QNetworkAccessManager, with the system's OpenSSL and CA
//   bundle.
// * **Android** — `HttpsURLConnection`, through the ~40-line
//   `android/src/com/kromakobra/cedarview/HttpGet.java`, called over JNI. Qt
//   has no native TLS backend on Android, so QNetworkAccessManager there would
//   need a bundled OpenSSL — and a bundled CA list, which goes stale and would
//   keep trusting a CA the phone's owner had distrusted. The platform's own
//   HTTP stack uses the phone's own trust store, so "trusted" here means
//   exactly what it means everywhere else on the device.
//
// Both: 20 s timeout, redirects followed, the same User-Agent,
// `Accept: application/json, */*`, and any non-2xx answer is a TransportError.
//
// Deliberately a separate library from the rest of the core (see
// src/core/CMakeLists.txt): it is the one piece that needs a network stack, and
// keeping it apart lets cedarview_core link Qt Core and nothing else.

#pragma once

#include "transport.h"

namespace mycu {

class HttpTransport : public Transport
{
public:
    explicit HttpTransport(int timeoutMs = 20000, QString userAgent = QString());

    Response get(const QString &path) override;

private:
    int m_timeoutMs;
    QString m_userAgent;
};

} // namespace mycu
