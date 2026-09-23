// Shared test setup.
//
// Two guarantees every test binary gets from CEDARVIEW_TEST_MAIN:
//
// 1. **No test can reach the network.** The application proxy is pointed at a
//    port nothing listens on, so any Qt Network request fails at once instead
//    of quietly reaching `selfservice.cedarville.edu`. A test that did that
//    would pass on your laptop, fail everywhere else, and — worse — make the
//    suite's result depend on whether you happen to be logged in. No test
//    constructs an HttpTransport either; a live request belongs in
//    scripts/check-live, not in the suite.
//
// 2. **Qt never needs a display.** QT_QPA_PLATFORM=offscreen is set before the
//    application object exists, so the Qt-touching tests run over SSH and in a
//    build sandbox.
//
// It also points MYCU_STATE_DIR at a throwaway directory, so no test can touch
// the real session in ~/.local/share/mycu.

#pragma once

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkProxy>
#include <QTemporaryDir>
#include <QtTest>

namespace testing {

inline QString fixturesDir()
{
    return QStringLiteral(CEDARVIEW_SOURCE_DIR "/tests/fixtures");
}

inline QString samplesDir()
{
    return fixturesDir() + QStringLiteral("/samples");
}

inline QString sourceDir()
{
    return QStringLiteral(CEDARVIEW_SOURCE_DIR);
}

inline QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("cannot read %s", qPrintable(path));
    return QString::fromUtf8(file.readAll());
}

inline QString fixture(const QString &name)
{
    return readText(fixturesDir() + u'/' + name);
}

inline QJsonValue fixtureJson(const QString &name)
{
    const QJsonDocument doc = QJsonDocument::fromJson(fixture(name).toUtf8());
    return doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
}

inline QString chapelHtml()
{
    return fixture(QStringLiteral("cedarinfo_chapelskip.html"));
}

inline QString loginHtml()
{
    return readText(samplesDir() + QStringLiteral("/entra_login_page.html"));
}

inline QString toJson(const QJsonValue &value)
{
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
}

inline void blockNetwork()
{
    QNetworkProxy::setApplicationProxy(
        QNetworkProxy(QNetworkProxy::Socks5Proxy, QStringLiteral("127.0.0.1"), 9));
}

} // namespace testing

// Assert that `expr` throws `ExceptionType` whose message contains `fragment`.
#define CV_VERIFY_THROWS_MATCHING(expr, ExceptionType, fragment)                                \
    do {                                                                                        \
        bool _caught = false;                                                                   \
        try {                                                                                   \
            (void)(expr);                                                                       \
        } catch (const ExceptionType &_e) {                                                     \
            _caught = true;                                                                     \
            const QString _message = QString::fromUtf8(_e.what());                             \
            if (!_message.contains(QStringLiteral(fragment)))                                   \
                QFAIL(qPrintable(QStringLiteral("message %1 does not mention %2")              \
                                     .arg(_message, QStringLiteral(fragment))));                \
        }                                                                                       \
        QVERIFY2(_caught, #expr " did not throw " #ExceptionType);                              \
    } while (false)

#define CEDARVIEW_TEST_MAIN(TestClass, ApplicationClass)                                        \
    int main(int argc, char **argv)                                                             \
    {                                                                                           \
        qputenv("QT_QPA_PLATFORM", "offscreen");                                                \
        qputenv("QT_FORCE_STDERR_LOGGING", "1");                                                \
        QTemporaryDir state;                                                                    \
        qputenv("MYCU_STATE_DIR", QFile::encodeName(state.path() + QStringLiteral("/state")));  \
        ApplicationClass app(argc, argv);                                                       \
        testing::blockNetwork();                                                                \
        TestClass tc;                                                                           \
        return QTest::qExec(&tc, argc, argv);                                                   \
    }
