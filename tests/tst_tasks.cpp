// Background task dispatch.

#include "testsupport.h"

#include "ui/tasks.h"

#include <stdexcept>

using namespace mycu;

class TestTasks : public QObject
{
    Q_OBJECT

private slots:
    void cleanup() { QThreadPool::globalInstance()->waitForDone(5000); }

    void aResultReachesTheCallback()
    {
        QObject context;
        std::optional<int> seen;
        runInBackground(
            &context, [] { return 42; }, [&](int value) { seen = value; },
            [&](std::exception_ptr) { seen = -1; });
        QTRY_VERIFY(seen.has_value());
        QCOMPARE(*seen, 42);
    }

    void anExceptionReachesTheErrorCallback()
    {
        QObject context;
        QString message;
        bool wasRuntimeError = false;
        runInBackground(
            &context, []() -> int { throw std::runtime_error("boom"); }, [](int) {},
            [&](std::exception_ptr error) {
                message = describe(error);
                try {
                    std::rethrow_exception(error);
                } catch (const std::runtime_error &) {
                    wasRuntimeError = true;
                } catch (...) {
                }
            });
        QTRY_VERIFY(!message.isEmpty());
        QCOMPARE(message, QStringLiteral("boom"));
        QVERIFY(wasRuntimeError);
    }

    // The exception types the viewmodels branch on must survive the trip
    // across threads intact, not arrive as a generic failure.
    void aTypedErrorKeepsItsType()
    {
        QObject context;
        int kind = 0;
        runInBackground(
            &context, []() -> int { throw SessionExpired("gone"); }, [](int) {},
            [&](std::exception_ptr error) {
                try {
                    std::rethrow_exception(error);
                } catch (const SessionExpired &) {
                    kind = 1;
                } catch (...) {
                    kind = 2;
                }
            });
        QTRY_VERIFY(kind != 0);
        QCOMPARE(kind, 1);
    }

    // Callbacks run on the context's thread — the GUI thread in the app — so
    // they are free to touch UI state.
    void callbacksRunOnTheContextsThread()
    {
        QObject context;
        QThread *ran = nullptr;
        QThread *worker = nullptr;
        runInBackground(
            &context, [&] { worker = QThread::currentThread(); return 0; },
            [&](int) { ran = QThread::currentThread(); }, [](std::exception_ptr) {});
        QTRY_VERIFY(ran != nullptr);
        QCOMPARE(ran, QThread::currentThread());
        QVERIFY(worker != QThread::currentThread());
    }

    void manyConcurrentTasksAllReport()
    {
        QObject context;
        QList<int> results;
        for (int n = 0; n < 12; ++n) {
            runInBackground(
                &context, [n] { return n * 2; }, [&](int value) { results.append(value); },
                [](std::exception_ptr) {});
        }
        QTRY_COMPARE(results.size(), 12);
        std::sort(results.begin(), results.end());
        for (int n = 0; n < 12; ++n)
            QCOMPARE(results[n], n * 2);
    }

    // If whatever asked for the work is gone by the time it finishes, the
    // result is dropped rather than delivered to a dead object.
    void aDestroyedContextIsNotCalledBack()
    {
        bool called = false;
        {
            QObject context;
            runInBackground(
                &context, [] { QThread::msleep(100); return 1; }, [&](int) { called = true; },
                [&](std::exception_ptr) { called = true; });
        }
        QThreadPool::globalInstance()->waitForDone(5000);
        QCoreApplication::processEvents();
        QTest::qWait(50);
        QVERIFY(!called);
    }
};

CEDARVIEW_TEST_MAIN(TestTasks, QCoreApplication)
#include "tst_tasks.moc"
