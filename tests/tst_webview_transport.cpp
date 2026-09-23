// The WebView transport, end to end against a real browser — without touching
// Cedarville.
//
// This exercises the single riskiest piece of the design:
//
//     QML surface  ->  evalAsync  ->  in-page fetch()  ->  window.__mycu[token]
//                  ->  poll  ->  evalResult  ->  Response on a worker thread
//
// with offscreen QtWebEngine and a throwaway HTTP server on 127.0.0.1. If this
// passes, the transport, the surface contract, the polling loop and the
// cross-thread handoff are all correct, and the only thing left that can go
// wrong with real data is authentication and parsing.
//
// Desktop only (QtWebEngine). On a phone, the same path is exercised by
// signing in.

#include "testsupport.h"

#include "ui/webviewtransport.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWebEngineProfile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include <functional>

using namespace mycu;

namespace {

// A tiny HTTP/1.0 server. Each path is a scenario.
class LoopbackServer : public QTcpServer
{
public:
    LoopbackServer()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = nextPendingConnection())
                handle(socket);
        });
    }

    QString origin() const { return QStringLiteral("http://127.0.0.1:%1").arg(serverPort()); }

    static QByteArray largeBody()
    {
        QJsonObject body{{"records", QJsonArray{QJsonObject{{"date", "2026-09-16"}, {"status", "absent"}}}},
                         {"note", QString(300000, u'x')}};
        return QJsonDocument(body).toJson(QJsonDocument::Compact);
    }

private:
    void handle(QTcpSocket *socket)
    {
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
            const QByteArray request = socket->readAll();
            const QByteArray path = request.split(' ').value(1);
            respond(socket, path);
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

    void respond(QTcpSocket *socket, const QByteArray &path)
    {
        auto send = [socket](int status, const QByteArray &type, const QByteArray &body) {
            QByteArray head = "HTTP/1.0 " + QByteArray::number(status) + " X\r\n";
            head += "Content-Type: " + type + "\r\n";
            head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
            head += "Connection: close\r\n\r\n";
            socket->write(head + body);
            socket->disconnectFromHost();
        };

        if (path.startsWith("/slow")) {
            QTimer::singleShot(1500, socket, [send] { send(200, "application/json", "{}"); });
        } else if (path.startsWith("/broken")) {
            send(500, "text/plain", "server error");
        } else if (path.startsWith("/signin")) {
            send(200, "text/html", "<html><form action=\"x\"><input name=\"SAMLRequest\"></form></html>");
        } else if (path.startsWith("/page")) {
            send(200, "text/html", "<html><body>loopback</body></html>");
        } else {
            send(200, "application/json", largeBody());
        }
    }
};

} // namespace

class TestWebViewTransport : public QObject
{
    Q_OBJECT

    LoopbackServer m_server;
    std::unique_ptr<QQuickWebEngineProfile> m_profile;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QObject *m_surface = nullptr;
    std::unique_ptr<WebViewTransport> m_transport;

    // Run `work` on a worker thread — get() refuses the GUI thread — while
    // this thread keeps the event loop, and therefore the browser, running.
    void onWorker(std::function<void()> work)
    {
        bool done = false;
        QThread *thread = QThread::create([&] {
            work();
            done = true;
        });
        thread->start();
        QTRY_VERIFY_WITH_TIMEOUT(done, 40000);
        thread->wait();
        delete thread;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_server.listen(QHostAddress::LocalHost));

        m_profile = std::make_unique<QQuickWebEngineProfile>();
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->rootContext()->setContextProperty("webProfile", m_profile.get());

        // The real surface, from the source tree, in a throwaway window.
        const QString qmlDir = QUrl::fromLocalFile(testing::sourceDir() + "/qml").toString();
        m_engine->loadData(QStringLiteral(R"(
            import QtQuick
            import QtQuick.Window
            import "%1" as Surfaces
            Window {
                width: 800; height: 600; visible: true
                Surfaces.WebSurfaceDesktop { objectName: "surface"; anchors.fill: parent }
            })").arg(qmlDir).toUtf8());
        QVERIFY2(!m_engine->rootObjects().isEmpty(), "the QML surface did not load at all");
        m_surface = m_engine->rootObjects().first()->findChild<QObject *>("surface");
        QVERIFY(m_surface);

        m_transport = std::make_unique<WebViewTransport>();
        m_transport->attachSurface(m_surface);

        // The fetch runs in the *page's* context, so a same-origin request
        // needs the page to be on that origin — the same reason the real app
        // aims the surface at Self-Service.
        QMetaObject::invokeMethod(m_surface, "navigate", Q_ARG(QVariant, m_server.origin() + "/page"));
        QTRY_VERIFY_WITH_TIMEOUT(m_transport->currentUrl().startsWith(m_server.origin()), 20000);
        QTest::qWait(500);
    }

    void cleanupTestCase()
    {
        m_transport->close();
        m_transport.reset();
        m_engine.reset();
        m_profile.reset();
    }

    void theSurfaceUrlIsFollowed() { QVERIFY(m_transport->currentUrl().endsWith("/page")); }

    void anInPageFetchReturnsALargeBody()
    {
        Response response;
        QString error;
        onWorker([&] {
            try {
                response = m_transport->get(m_server.origin() + "/data");
            } catch (const std::exception &e) {
                error = QString::fromUtf8(e.what());
            }
        });
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(response.status, 200);
        QVERIFY2(response.body.size() > 250000, "a large body must survive runJavaScript");
        QVERIFY(response.json().isObject());
        QVERIFY(response.headers.value("content-type").contains("json"));
        QCOMPARE(response.url, m_server.origin() + "/data");
    }

    void consecutiveRequestsAreNotConfused()
    {
        QString a, b;
        onWorker([&] {
            a = m_transport->get(m_server.origin() + "/a").url;
            b = m_transport->get(m_server.origin() + "/b").url;
        });
        QVERIFY(a.endsWith("/a"));
        QVERIFY(b.endsWith("/b"));
    }

    void concurrentRequestsAreNotConfused()
    {
        QString a, b;
        bool doneA = false, doneB = false;
        QThread *first = QThread::create([&] { a = m_transport->get(m_server.origin() + "/one").url; doneA = true; });
        QThread *second = QThread::create([&] { b = m_transport->get(m_server.origin() + "/two").url; doneB = true; });
        first->start();
        second->start();
        QTRY_VERIFY_WITH_TIMEOUT(doneA && doneB, 40000);
        first->wait();
        second->wait();
        delete first;
        delete second;
        QVERIFY(a.endsWith("/one"));
        QVERIFY(b.endsWith("/two"));
    }

    // A stalled request must throw, not hang forever.
    void aStalledRequestTimesOutInsteadOfHanging()
    {
        QString outcome;
        onWorker([&] {
            try {
                m_transport->get(m_server.origin() + "/slow", 300);
                outcome = "did not throw";
            } catch (const TransportError &) {
                outcome = "TransportError";
            } catch (const std::exception &e) {
                outcome = QString::fromUtf8(e.what());
            }
        });
        QCOMPARE(outcome, QStringLiteral("TransportError"));
    }

    void anErrorStatusIsATransportError()
    {
        QString outcome;
        onWorker([&] {
            try {
                m_transport->get(m_server.origin() + "/broken");
            } catch (const TransportError &e) {
                outcome = QString::fromUtf8(e.what());
            }
        });
        QVERIFY2(outcome.contains("HTTP 500"), qPrintable(outcome));
    }

    // A sign-in page arriving where data was expected is an expired session,
    // and the transport says so on its own signal too, so the sign-in surface
    // can open before the exception has made its way back.
    void aSignInPageIsAnExpiredSession()
    {
        QSignalSpy expired(m_transport.get(), &WebViewTransport::sessionExpired);
        bool threw = false;
        onWorker([&] {
            try {
                m_transport->get(m_server.origin() + "/signin");
            } catch (const SessionExpired &) {
                threw = true;
            }
        });
        QVERIFY(threw);
        QTRY_COMPARE(expired.count(), 1);
    }

    // A refused request — here, cross-origin — gets a second look at where the
    // page really is. On our own loopback page that is not a sign-in, so the
    // honest answer is a transport error, not a trip to the login screen.
    void aRefusedRequestThatIsNotASignInStaysATransportError()
    {
        QString outcome;
        onWorker([&] {
            try {
                m_transport->get(QStringLiteral("http://localhost:%1/data").arg(m_server.serverPort()));
            } catch (const SessionExpired &) {
                outcome = "SessionExpired";
            } catch (const TransportError &e) {
                outcome = QString::fromUtf8(e.what());
            }
        });
        QVERIFY2(outcome.startsWith("fetch failed for"), qPrintable(outcome));
    }

    void evaluateReturnsTheLastExpression()
    {
        QVariant href;
        QVariant sum;
        onWorker([&] {
            href = m_transport->evaluate("window.location.href");
            sum = m_transport->evaluate("1 + 2");
        });
        QVERIFY(href.toString().startsWith(m_server.origin()));
        QCOMPARE(sum.toInt(), 3);
    }

    // get() blocks waiting for the GUI thread; on the GUI thread it would
    // deadlock, so it refuses instead.
    void theGuiThreadIsRefused()
    {
        QVERIFY_THROWS_EXCEPTION(TransportError, m_transport->get(m_server.origin() + "/data"));
        QVERIFY_THROWS_EXCEPTION(TransportError, m_transport->evaluate("1"));
    }
};

// Not CEDARVIEW_TEST_MAIN: QtWebEngine has to be initialised before the
// application object exists, and the network block is left off — this test
// only ever talks to 127.0.0.1, and Chromium honours Qt's application proxy,
// which would stop it doing even that.
int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    TestWebViewTransport tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_webview_transport.moc"
