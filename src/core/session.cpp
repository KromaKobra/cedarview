#include "session.h"

#include "log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <cstdio>

namespace mycu {

namespace {

double now()
{
    return QDateTime::currentMSecsSinceEpoch() / 1000.0;
}

} // namespace

QString defaultStateDir()
{
    const QString override = qEnvironmentVariable("MYCU_STATE_DIR");
    if (!override.isEmpty())
        return override;

#ifdef Q_OS_ANDROID
    // `<APPROOT>/files` on Android: app-private and unreadable by other apps,
    // which is what we want.
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(APP_DIR_NAME);
#else
    const QString xdg = qEnvironmentVariable("XDG_DATA_HOME");
    const QString base = !xdg.isEmpty() ? xdg : QDir::home().filePath(QStringLiteral(".local/share"));
    return QDir(base).filePath(APP_DIR_NAME);
#endif
}

SessionStore::SessionStore(const QString &stateDir)
    : m_stateDir(stateDir.isEmpty() ? defaultStateDir() : stateDir)
{}

QString SessionStore::path() const
{
    return QDir(m_stateDir).filePath(QLatin1StringView(FILENAME));
}

QString SessionStore::profileDir() const
{
    return QDir(m_stateDir).filePath(QStringLiteral("webprofile"));
}

void SessionStore::ensureDirs() const
{
    if (QDir(m_stateDir).exists())
        return;
    QDir().mkpath(m_stateDir);
    QFile::setPermissions(m_stateDir,
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

SessionState SessionStore::load() const
{
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return {};

    const QJsonObject raw = doc.object();
    const QJsonValue schema = raw.value(QStringLiteral("schema"));
    if (!schema.isDouble() || schema.toDouble() != SCHEMA_VERSION)
        return {};

    // Unknown keys from a future version are ignored; known ones are read.
    SessionState state;
    state.lastLogin = raw.value(QStringLiteral("last_login")).toDouble(0.0);
    state.lastSuccess = raw.value(QStringLiteral("last_success")).toDouble(0.0);
    state.lastExpiry = raw.value(QStringLiteral("last_expiry")).toDouble(0.0);
    state.lastTerm = raw.value(QStringLiteral("last_term")).toString();
    state.schema = SCHEMA_VERSION;
    return state;
}

void SessionStore::save(const SessionState &state) const
{
    ensureDirs();

    QJsonObject raw;
    raw.insert(QStringLiteral("last_login"), state.lastLogin);
    raw.insert(QStringLiteral("last_success"), state.lastSuccess);
    raw.insert(QStringLiteral("last_expiry"), state.lastExpiry);
    raw.insert(QStringLiteral("last_term"), state.lastTerm);
    raw.insert(QStringLiteral("schema"), state.schema);

    const QString target = path();
    const QString tmp = target + QStringLiteral(".tmp");
    {
        QFile file(tmp);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCWarning(lcSession) << "could not write" << tmp << ":" << file.errorString();
            return;
        }
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        file.write(QJsonDocument(raw).toJson(QJsonDocument::Indented));
    }
    QFile::setPermissions(tmp, QFile::ReadOwner | QFile::WriteOwner);

    // rename(2) replaces the target atomically; QFile::rename refuses to.
    if (std::rename(QFile::encodeName(tmp).constData(), QFile::encodeName(target).constData()) != 0)
        qCWarning(lcSession) << "could not replace" << target;
}

SessionState &SessionStore::markLogin(SessionState &state) const
{
    state.lastLogin = now();
    save(state);
    return state;
}

SessionState &SessionStore::markSuccess(SessionState &state) const
{
    state.lastSuccess = now();
    save(state);
    return state;
}

SessionState &SessionStore::markExpiry(SessionState &state) const
{
    state.lastExpiry = now();
    save(state);
    return state;
}

void SessionStore::clear() const
{
    QFile::remove(path());
}

} // namespace mycu
