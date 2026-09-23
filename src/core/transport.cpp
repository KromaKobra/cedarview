#include "transport.h"

#include "log.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

namespace mycu {

namespace {

// Python's repr() of a string, near enough for error messages: single quotes.
QString quoted(const QString &text)
{
    QString escaped = text;
    escaped.replace(u'\\', QStringLiteral("\\\\")).replace(u'\'', QStringLiteral("\\'"));
    return u'\'' + escaped + u'\'';
}

} // namespace

QJsonValue Response::json() const
{
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || doc.isNull()) {
        QString preview = body.left(200);
        preview.replace(u'\n', u' ');
        throw ParseError(QStringLiteral("expected JSON from %1 but got %2")
                             .arg(quoted(url), quoted(preview)));
    }
    if (doc.isArray())
        return doc.array();
    return doc.object();
}

const Response &Response::raiseForSession() const
{
    if (looksLikeLogin(url, body)) {
        throw SessionExpired(QStringLiteral("request for %1 landed on an identity provider; "
                                            "the Self-Service session is gone")
                                 .arg(quoted(url)));
    }
    return *this;
}

QString resolve(const QString &path)
{
    if (path.startsWith(u"http://") || path.startsWith(u"https://"))
        return path;
    QString relative = path;
    while (relative.startsWith(u'/'))
        relative.remove(0, 1);
    return BASE_URL + u'/' + relative;
}

bool isSelfservice(const QString &url)
{
    return url.startsWith(BASE_URL);
}

QString originOf(const QString &urlOrPath)
{
    const QUrl parsed(resolve(urlOrPath));
    return parsed.scheme() + QStringLiteral("://") + parsed.authority();
}

bool looksLikeLogin(const QString &url, const QString &body)
{
    // Match on the HOST, never on the whole URL string. A substring test looks
    // equivalent and is not: analytics and tracking URLs routinely carry a
    // `ref=https://login.microsoftonline.com/` parameter, and a Self-Service
    // page whose URL happened to include one would be declared expired —
    // kicking the user to a sign-in they did not need. Observed for real in a
    // capture: `t.vibe.co/pixel/s?...&ref=https://login.microsoftonline.com/`
    // raised SessionExpired.
    const QString host = QUrl(url).host().toLower();
    if (!host.isEmpty()) {
        for (const QString &idp : IDP_HOSTS) {
            if (host == idp || host.endsWith(u'.' + idp))
                return true;
        }
    }

    if (!body.isEmpty()) {
        const QString head = body.left(8192);
        for (const QString &marker : LOGIN_BODY_MARKERS) {
            if (head.contains(marker))
                return true;
        }
        // A page whose <form> posts to an IdP is a login page regardless of
        // which host served it.
        static const QRegularExpression formToIdp(
            QStringLiteral(R"(<form[^>]+action="https://login\.microsoft)"),
            QRegularExpression::CaseInsensitiveOption);
        if (formToIdp.match(head).hasMatch())
            return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// FixtureTransport
// ---------------------------------------------------------------------------

FixtureTransport::FixtureTransport(QString root, int status)
    : m_root(std::move(root))
    , m_status(status)
{}

QString FixtureTransport::slug(const QString &path)
{
    const QUrl parsed(resolve(path));
    QString cleaned = parsed.path();
    while (cleaned.startsWith(u'/'))
        cleaned.remove(0, 1);
    while (cleaned.endsWith(u'/'))
        cleaned.chop(1);
    if (parsed.scheme() + QStringLiteral("://") + parsed.authority() != BASE_URL)
        cleaned = parsed.authority() + u'/' + cleaned;

    static const QRegularExpression separators(QStringLiteral("[^A-Za-z0-9]+"));
    cleaned.replace(separators, QStringLiteral("_"));
    while (cleaned.startsWith(u'_'))
        cleaned.remove(0, 1);
    while (cleaned.endsWith(u'_'))
        cleaned.chop(1);
    cleaned = cleaned.toLower();
    return cleaned.isEmpty() ? QStringLiteral("index") : cleaned;
}

Response FixtureTransport::get(const QString &path)
{
    const QString stem = slug(path);
    const QDir dir(m_root);
    for (const char *suffix : {".json", ".html", ".txt"}) {
        QFile candidate(dir.filePath(stem + QLatin1StringView(suffix)));
        if (!candidate.exists())
            continue;
        if (!candidate.open(QIODevice::ReadOnly))
            throw FixtureNotFound(QStringLiteral("could not read fixture %1").arg(candidate.fileName()));

        Response response;
        response.status = m_status;
        response.url = resolve(path);
        response.body = QString::fromUtf8(candidate.readAll());
        response.headers.insert(QStringLiteral("content-type"),
                                qstrcmp(suffix, ".json") == 0 ? QStringLiteral("application/json")
                                                              : QStringLiteral("text/html"));
        return response;
    }

    const QStringList available = dir.exists()
        ? dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)
        : QStringList();
    throw FixtureNotFound(QStringLiteral("no fixture for '%1' (looked for %2.json/.html/.txt in "
                                         "%3); available: [%4]")
                              .arg(path, stem, m_root, available.join(QStringLiteral(", "))));
}

// ---------------------------------------------------------------------------
// TransportRouter
// ---------------------------------------------------------------------------

TransportRouter::TransportRouter(TransportPtr fallback)
    : m_default(std::move(fallback))
{}

TransportRouter &TransportRouter::route(const QString &origin, TransportPtr transport)
{
    QString key = origin;
    while (key.endsWith(u'/'))
        key.chop(1);
    m_routes.insert(key, std::move(transport));
    return *this;
}

Transport *TransportRouter::transportFor(const QString &path) const
{
    const auto it = m_routes.constFind(originOf(path));
    return it != m_routes.cend() ? it->get() : m_default.get();
}

Response TransportRouter::get(const QString &path)
{
    return transportFor(path)->get(path);
}

} // namespace mycu
