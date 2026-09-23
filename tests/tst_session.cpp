// Session metadata persistence.
//
// Reminder of what this is *not*: it does not hold cookies or credentials. The
// cookie jar belongs to the WebView. See src/core/session.h for why that is
// forced on us rather than chosen.

#include "testsupport.h"

#include "core/session.h"

using namespace mycu;

namespace {

void writeFile(const QString &path, const QByteArray &text)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}

struct EnvGuard
{
    QByteArray name, saved;
    bool had;
    explicit EnvGuard(const char *n) : name(n), saved(qgetenv(n)), had(qEnvironmentVariableIsSet(n)) {}
    ~EnvGuard()
    {
        if (had)
            qputenv(name.constData(), saved);
        else
            qunsetenv(name.constData());
    }
};

} // namespace

class TestSession : public QObject
{
    Q_OBJECT

private slots:
    void aFreshStoreReportsNoSession()
    {
        QTemporaryDir dir;
        const SessionState state = SessionStore(dir.path()).load();
        QVERIFY(!state.hasLoggedIn());
        QCOMPARE(state.lastLogin, 0.0);
    }

    void roundTrip()
    {
        QTemporaryDir dir;
        SessionStore store(dir.path());
        SessionState state = store.load();
        state.lastTerm = "Fall 2026";
        store.markLogin(state);

        const SessionState reloaded = SessionStore(dir.path()).load();
        QVERIFY(reloaded.hasLoggedIn());
        QCOMPARE(reloaded.lastTerm, QStringLiteral("Fall 2026"));
    }

    void stateFileIsNotWorldReadable()
    {
        QTemporaryDir dir;
        SessionStore store(dir.path());
        store.save(store.load());
        QCOMPARE(QFile::permissions(store.path()),
                 QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
    }

    void theStateDirIsCreatedPrivate()
    {
        QTemporaryDir dir;
        SessionStore store(dir.filePath("nested/state"));
        store.ensureDirs();
        const QFile::Permissions perms = QFile::permissions(store.stateDir());
        QVERIFY(perms & QFile::ExeOwner);
        QVERIFY(!(perms & (QFile::ReadOther | QFile::ReadGroup)));
    }

    // An app that cannot start without a terminal is worse than an app that
    // asks you to log in once more.
    void aCorruptFileIsTreatedAsNoSessionNotACrash()
    {
        QTemporaryDir dir;
        SessionStore store(dir.path());
        store.ensureDirs();
        writeFile(store.path(), "{ this is not json");
        QVERIFY(!store.load().hasLoggedIn());
    }

    void anOlderSchemaIsDiscarded()
    {
        QTemporaryDir dir;
        SessionStore store(dir.path());
        store.ensureDirs();
        writeFile(store.path(), QByteArray(R"({"schema": )") + QByteArray::number(SCHEMA_VERSION - 1)
                                    + R"(, "last_login": 123.0})");
        QVERIFY(!store.load().hasLoggedIn());
    }

    void unknownKeysFromAFutureVersionAreIgnored()
    {
        QTemporaryDir dir;
        SessionStore store(dir.path());
        store.ensureDirs();
        writeFile(store.path(), QByteArray(R"({"schema": )") + QByteArray::number(SCHEMA_VERSION)
                                    + R"(, "last_login": 5.0, "something_new": true})");
        QVERIFY(store.load().hasLoggedIn());
    }

    // The Python app wrote this file; a desktop user upgrading must stay
    // signed in, so the C++ reader must accept exactly that shape.
    void aFileWrittenByThePythonAppIsRead()
    {
        QTemporaryDir dir;
        SessionStore store(dir.path());
        store.ensureDirs();
        writeFile(store.path(), R"({
  "last_login": 1789600000.123,
  "last_success": 1789600100.5,
  "last_expiry": 0.0,
  "last_term": "Fall Semester 2026",
  "schema": 1
})");
        const SessionState state = store.load();
        QVERIFY(state.hasLoggedIn());
        QCOMPARE(state.lastTerm, QStringLiteral("Fall Semester 2026"));
        QCOMPARE(state.lastSuccess, 1789600100.5);
    }

    void theWrittenFileKeepsTheSameKeys()
    {
        QTemporaryDir dir;
        SessionStore store(dir.path());
        store.save(SessionState{1.0, 2.0, 3.0, "T", SCHEMA_VERSION});
        const QJsonObject raw = QJsonDocument::fromJson(testing::readText(store.path()).toUtf8()).object();
        QStringList keys = raw.keys();
        keys.sort();
        QCOMPARE(keys, (QStringList{"last_expiry", "last_login", "last_success", "last_term", "schema"}));
    }

    void clearForgetsEverything()
    {
        QTemporaryDir dir;
        SessionStore store(dir.path());
        SessionState state = store.load();
        store.markLogin(state);
        store.clear();
        QVERIFY(!store.load().hasLoggedIn());
    }

    void clearIsSafeWhenNothingIsSaved()
    {
        QTemporaryDir dir;
        SessionStore(dir.path()).clear();
    }

    void profileDirSitsUnderTheStateDir()
    {
        QTemporaryDir dir;
        QCOMPARE(QFileInfo(SessionStore(dir.path()).profileDir()).absolutePath(),
                 QFileInfo(dir.path()).absoluteFilePath());
    }

    void stateDirHonoursTheEnvOverride()
    {
        QTemporaryDir dir;
        EnvGuard guard("MYCU_STATE_DIR");
        qputenv("MYCU_STATE_DIR", QFile::encodeName(dir.filePath("elsewhere")));
        QCOMPARE(defaultStateDir(), dir.filePath("elsewhere"));
        QCOMPARE(SessionStore().stateDir(), dir.filePath("elsewhere"));
    }

    // The same path the Python app used, so an existing sign-in survives.
    void desktopStateFollowsXdg()
    {
        QTemporaryDir dir;
        EnvGuard state("MYCU_STATE_DIR");
        EnvGuard xdg("XDG_DATA_HOME");
        qunsetenv("MYCU_STATE_DIR");
        qputenv("XDG_DATA_HOME", QFile::encodeName(dir.filePath("xdg")));
        QCOMPARE(defaultStateDir(), dir.filePath("xdg/mycu"));
    }

    void desktopStateDefaultsToLocalShare()
    {
        EnvGuard state("MYCU_STATE_DIR");
        EnvGuard xdg("XDG_DATA_HOME");
        qunsetenv("MYCU_STATE_DIR");
        qunsetenv("XDG_DATA_HOME");
        QCOMPARE(defaultStateDir(), QDir::home().filePath(".local/share/mycu"));
    }

    void stateDefaultsAreAllFalsey()
    {
        const SessionState state;
        QCOMPARE(state.lastLogin, 0.0);
        QCOMPARE(state.lastSuccess, 0.0);
        QCOMPARE(state.lastExpiry, 0.0);
        QCOMPARE(state.schema, SCHEMA_VERSION);
    }
};

CEDARVIEW_TEST_MAIN(TestSession, QCoreApplication)
#include "tst_session.moc"
