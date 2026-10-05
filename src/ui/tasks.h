// Running blocking work off the GUI thread.
//
// Providers are synchronous by design — that is what lets them be tested with a
// fixture and no event loop. But WebViewTransport::get() blocks waiting on the
// GUI thread, so a provider must never run *on* the GUI thread. This is the one
// place that rule is enforced.
//
// Deliberately tiny: the work goes to the global QThreadPool, and its result or
// exception is delivered back on `context`'s thread (the GUI thread, for every
// caller in the app) through a queued call. If `context` is destroyed first,
// the delivery is dropped rather than calling into a dead object.

#pragma once

#include "core/errors.h"
#include "core/log.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QThreadPool>

#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

namespace mycu {

using ErrorHandler = std::function<void(std::exception_ptr)>;

// Run `work` on a pool thread; call `onDone(result)` or `onError(exception)`
// back on `context`'s thread. Call it from that thread.
template <typename Work, typename Done>
void runInBackground(QObject *context, Work work, Done onDone, ErrorHandler onError)
{
    using Result = std::invoke_result_t<Work &>;
    Q_ASSERT(context && context->thread() == QThread::currentThread());

    // The worker posts its answer to `relay`, never to `context` itself:
    // `context` may be destroyed while the work runs, and a queued call aimed
    // at a dead object is a crash. `relay` has no parent and is deleted only
    // by that queued call, on this thread, so it is alive whenever the worker
    // posts to it — and the check that `context` still exists happens on the
    // context's own thread, where it cannot race with its deletion.
    auto *relay = new QObject;
    const QPointer<QObject> guard(context);

    QThreadPool::globalInstance()->start(
        [relay, guard, work = std::move(work), onDone = std::move(onDone),
         onError = std::move(onError)]() mutable {
            try {
                Result result = work();
                QMetaObject::invokeMethod(
                    relay,
                    [relay, guard, onDone = std::move(onDone), result = std::move(result)]() mutable {
                        relay->deleteLater();
                        if (guard)
                            onDone(std::move(result));
                    },
                    Qt::QueuedConnection);
            } catch (...) {
                const std::exception_ptr error = std::current_exception();
                // Often the only clue on Android, where logcat is the debugger.
                qCWarning(lcTasks).noquote() << "background task failed:" << describe(error);
                QMetaObject::invokeMethod(
                    relay,
                    [relay, guard, onError = std::move(onError), error]() {
                        relay->deleteLater();
                        if (guard)
                            onError(error);
                    },
                    Qt::QueuedConnection);
            }
        });
}

// The same, for work that has something worth showing before it is finished —
// the chapel schedule's first page, say. `work` is called with a function it
// may call (from the pool thread) with an early result; each one reaches
// `onPartial` on `context`'s thread, in order, and always before `onDone` or
// `onError`. Partial results go through the same relay as the final one, which
// is what keeps them in order and keeps a destroyed `context` from being
// called.
template <typename Partial, typename Work, typename Done>
void runInBackgroundWithPartial(QObject *context, Work work, std::function<void(const Partial &)> onPartial,
                                Done onDone, ErrorHandler onError)
{
    Q_ASSERT(context && context->thread() == QThread::currentThread());

    auto *relay = new QObject;
    const QPointer<QObject> guard(context);
    // Shared by the work (which posts) and the queued calls (which run it).
    auto partial = std::make_shared<std::function<void(const Partial &)>>(std::move(onPartial));
    std::function<void(const Partial &)> report = [relay, guard, partial](const Partial &value) {
        QMetaObject::invokeMethod(
            relay,
            [guard, partial, value]() {
                if (guard)
                    (*partial)(value);
            },
            Qt::QueuedConnection);
    };

    runInBackground(
        relay, [work = std::move(work), report]() mutable { return work(report); },
        [relay, guard, onDone = std::move(onDone)](auto &&result) mutable {
            relay->deleteLater();
            if (guard)
                onDone(std::forward<decltype(result)>(result));
        },
        [relay, guard, onError = std::move(onError)](std::exception_ptr error) {
            relay->deleteLater();
            if (guard)
                onError(error);
        });
}

} // namespace mycu
