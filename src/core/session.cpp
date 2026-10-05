#include "session.h"

#include "jsonfile.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QStandardPaths>

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
    ensurePrivateDir(m_stateDir);
}

SessionState SessionStore::load() const
{
    const std::optional<QJsonObject> file = readJsonObject(path());
    if (!file)
        return {};
    const QJsonObject &raw = *file;

    // v1 had a subset of v2's keys and the same meaning for each, so it is
    // read as it stands; see SCHEMA_VERSION.
    const QJsonValue schema = raw.value(QStringLiteral("schema"));
    if (!schema.isDouble() || (schema.toDouble() != SCHEMA_VERSION && schema.toDouble() != 1))
        return {};

    // Unknown keys from a future version are ignored; known ones are read.
    SessionState state;
    state.lastLogin = raw.value(QStringLiteral("last_login")).toDouble(0.0);
    state.lastSuccess = raw.value(QStringLiteral("last_success")).toDouble(0.0);
    state.lastExpiry = raw.value(QStringLiteral("last_expiry")).toDouble(0.0);
    state.lastTerm = raw.value(QStringLiteral("last_term")).toString();
    state.studentId = raw.value(QStringLiteral("student_id")).toString();
    state.mealsPersonId = raw.value(QStringLiteral("meals_person_id")).toString();
    state.mealsCard = raw.value(QStringLiteral("meals_card")).toString();
    state.schema = SCHEMA_VERSION;
    state.fromV1 = schema.toDouble() == 1;
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
    raw.insert(QStringLiteral("student_id"), state.studentId);
    raw.insert(QStringLiteral("meals_person_id"), state.mealsPersonId);
    raw.insert(QStringLiteral("meals_card"), state.mealsCard);
    raw.insert(QStringLiteral("schema"), state.schema);
    atomicWriteJson(path(), raw, QJsonDocument::Indented);
}

SessionState SessionStore::update(const std::function<void(SessionState &)> &change) const
{
    SessionState state = load();
    change(state);
    save(state);
    state.fromV1 = false;
    return state;
}

SessionState SessionStore::markLogin() const
{
    return update([](SessionState &state) { state.lastLogin = now(); });
}

SessionState SessionStore::markSuccess() const
{
    return update([](SessionState &state) { state.lastSuccess = now(); });
}

SessionState SessionStore::markExpiry() const
{
    return update([](SessionState &state) { state.lastExpiry = now(); });
}

void SessionStore::clear() const
{
    QFile::remove(path());
}

} // namespace mycu
