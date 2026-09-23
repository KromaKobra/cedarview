// The transport seam.
//
// A Transport fetches a path on `selfservice.cedarville.edu` and hands back a
// string. That is the entire contract. Providers are written against it and
// therefore never learn that a WebView is involved, which is why they can be
// tested against saved fixtures with no GUI and no network.
//
// The implementations:
//
// * FixtureTransport (here, in the core) — serves files from `tests/fixtures/`.
//   Used by the test suite and by `--demo` mode.
// * WebViewTransport (src/ui) — evaluates `fetch()` inside the logged-in page.
//   Identical code on desktop and Android.
// * HttpTransport (httptransport.h) — plain HTTPS, for the public services.
// * TransportRouter — sends each request to one of the above by origin.
//
// Session expiry is *detected, not predicted*: see looksLikeLogin().

#pragma once

#include "errors.h"

#include <QHash>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <memory>
#include <stdexcept>

namespace mycu {

// Ellucian Colleague Self-Service. A single SAML Service Provider, so one login
// covers chapel, grades, schedule and the student account alike. This is the
// default origin: a bare path like `/cedarinfo/chapelskip` resolves here.
inline const QString BASE_URL = QStringLiteral("https://selfservice.cedarville.edu");

// Cedarville's dining menu service. **Unauthenticated** — its own page fetches
// it with `credentials: "omit"`. Verified 2026-09-16: `/api/menus?days=N`
// returns `{"YYYY-MM-DD": [{venue, meal, slot, items}]}`. Because there is no
// session involved, this origin is served by HttpTransport, not through the
// WebView — see TransportRouter.
inline const QString DINING_BASE = QStringLiteral("https://diningdata.cedarville.edu");

// Hosts that mean "you are not logged in". Landing on any of these is the
// definitive expiry signal — far more robust than tracking cookie lifetimes,
// because Entra ID's session policy can change without telling us.
inline const QStringList IDP_HOSTS = {
    QStringLiteral("login.microsoftonline.com"),
    QStringLiteral("login.microsoft.com"),
    QStringLiteral("login.windows.net"),
    QStringLiteral("sts.cedarville.edu"),
    QStringLiteral("adfs.cedarville.edu"),
};

// Markers that appear in a login page body but should never appear in a
// Self-Service data page. Used only as a fallback when the final URL is
// unavailable or inconclusive.
inline const QStringList LOGIN_BODY_MARKERS = {
    QStringLiteral("SAMLRequest"),
    QStringLiteral("urn:oasis:names:tc:SAML"),
    QStringLiteral("$Config={\"fShowPersistentCookiesWarning\""), // Entra ID sign-in bootstrap
    QStringLiteral("ConvergedSignIn"),
    QStringLiteral("Sign in to your account"),
};

// Sent by the direct HTTP requests. Honest about what this is: a personal
// client reading one student's own records.
inline const QString USER_AGENT_SUFFIX = QStringLiteral("CedarView/0.1 (personal student-records client)");

// What a transport returns.
//
// `url` is the *final* URL and is the load-bearing field. After a redirect
// chain, it is the only reliable way to know whether we ended up on
// Self-Service or got bounced to the identity provider.
struct Response
{
    int status = 0;
    QString url;
    QString body;
    QHash<QString, QString> headers;

    bool ok() const { return status >= 200 && status < 300; }

    // Parse the body as JSON (an object or an array), throwing ParseError on
    // failure.
    //
    // The failure message includes a body prefix, because the single most
    // common cause is an HTML login page arriving where JSON was expected.
    QJsonValue json() const;

    // Throw SessionExpired if this response is really a login page.
    //
    // Call this before parsing. Returns `*this` so it can be chained.
    const Response &raiseForSession() const;
};

// Fetch a Self-Service path and return the response.
//
// `path` may be absolute (`https://selfservice.cedarville.edu/...`) or
// root-relative (`/cedarinfo/chapelskip`). Implementations resolve it against
// BASE_URL.
//
// Implementations throw SessionExpired when the request lands on an identity
// provider, and TransportError for anything else that goes wrong.
class Transport
{
public:
    virtual ~Transport() = default;
    virtual Response get(const QString &path) = 0;
};

using TransportPtr = std::shared_ptr<Transport>;

// Turn a path into an absolute Self-Service URL.
//
//   resolve("/cedarinfo/chapelskip")
//     -> "https://selfservice.cedarville.edu/cedarinfo/chapelskip"
QString resolve(const QString &path);

// Whether `url` is on the Self-Service origin.
bool isSelfservice(const QString &url);

// The scheme+host a path will actually be requested from.
//
//   originOf("/cedarinfo/chapelskip") -> "https://selfservice.cedarville.edu"
QString originOf(const QString &urlOrPath);

// Decide whether we are looking at a sign-in page rather than data.
//
// Two independent signals, checked in order of reliability:
//
// 1. **The final URL is on a known identity provider host.** Decisive. After
//    following redirects, an unauthenticated Self-Service request always ends
//    at Entra ID.
// 2. **The body carries SAML/Entra sign-in markers.** A fallback for the case
//    where the URL is missing or the IdP round-trips us back through
//    Self-Service with a form post.
//
// A short body is *not* treated as a signal — empty responses happen for
// unrelated reasons, and guessing would send the user to a login page they did
// not need.
bool looksLikeLogin(const QString &url, const QString &body = QString());

// A fixture file that is not there. Deliberately *not* a MycuError: it means a
// fixture needs creating, not that Cedarville changed anything, and the
// viewmodels report it as an unexpected error.
class FixtureNotFound : public std::runtime_error
{
public:
    explicit FixtureNotFound(const QString &message)
        : std::runtime_error(message.toStdString())
    {}
};

// A Transport that serves saved response bodies from disk.
//
// This is how every parsing test runs: no GUI, no network, no login. It is also
// what `cedarview --demo` uses, so the UI can be developed and screenshotted
// before a phone or a real session is in the picture.
//
// Files are looked up by a slug derived from the path:
//
//     /cedarinfo/chapelskip  ->  fixtures/cedarinfo_chapelskip.html
//                                fixtures/cedarinfo_chapelskip.json
//
// The `.json` extension wins if both exist, so dropping a real JSON capture
// next to the placeholder HTML is all it takes to switch the provider over.
class FixtureTransport : public Transport
{
public:
    explicit FixtureTransport(QString root, int status = 200);

    // Filename stem for a path.
    //
    // Paths on the default origin keep their bare form; anything else is
    // prefixed with its host, so fixtures from different services cannot
    // collide:
    //
    //   slug("/cedarinfo/chapelskip") -> "cedarinfo_chapelskip"
    //   slug("https://diningdata.cedarville.edu/api/menus?days=2")
    //     -> "diningdata_cedarville_edu_api_menus"
    //
    // The query string is dropped deliberately — `?days=2` and `?days=7` are
    // the same endpoint, and a fixture per parameter value would be noise.
    static QString slug(const QString &path);

    Response get(const QString &path) override;

    QString root() const { return m_root; }

private:
    QString m_root;
    int m_status;
};

// Sends each request to the transport that suits its origin.
//
// The app talks to more than one service, and they do not all authenticate the
// same way:
//
//   Origin                           Auth               Transport
//   selfservice.cedarville.edu       SAML / Entra ID    WebView
//   diningdata.cedarville.edu        none               plain HTTP
//   mediaserve.cedarville.edu        none               plain HTTP
//
// Providers stay unaware of all of this: they declare a path and call
// `transport.get(path)`, exactly as before.
//
// A second reason this class exists: an in-page `fetch()` is subject to the
// same-origin policy. A WebView parked on Self-Service *cannot* fetch the
// dining API — the browser would block it before the request left. Routing by
// origin is not an optimisation, it is a correctness requirement.
class TransportRouter : public Transport
{
public:
    explicit TransportRouter(TransportPtr fallback);

    // Send everything on `origin` to `transport`. Chainable.
    TransportRouter &route(const QString &origin, TransportPtr transport);

    Transport *transportFor(const QString &path) const;

    Response get(const QString &path) override;

private:
    TransportPtr m_default;
    QHash<QString, TransportPtr> m_routes;
};

} // namespace mycu
