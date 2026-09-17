"""Running blocking work off the GUI thread.

Providers are synchronous by design — that is what lets them be tested with a
fixture and no Qt. But :meth:`mycu.ui.transport_webview.WebViewTransport.get`
blocks waiting on the GUI thread, so a provider must never run *on* the GUI
thread. This module is the one place that rule is enforced.

Deliberately tiny: a ``QRunnable`` on the global ``QThreadPool``, with results
delivered back to the GUI thread through signals (Qt auto-connections queue
across threads, so the callbacks are safe to touch UI state).
"""

from __future__ import annotations

import logging
import threading
import traceback
from typing import Callable

from PySide6.QtCore import QObject, QRunnable, QThreadPool, Signal, Slot

log = logging.getLogger(__name__)


class _TaskSignals(QObject):
    done = Signal(object)
    failed = Signal(object)


class _Task(QRunnable):
    def __init__(self, fn: Callable[[], object]) -> None:
        super().__init__()
        self._fn = fn
        self.signals = _TaskSignals()

    @Slot()
    def run(self) -> None:  # pragma: no cover - exercised on a real thread
        try:
            result = self._fn()
        except Exception as exc:  # noqa: BLE001 - deliberately catch-all
            # A traceback logged here is often the only clue on Android, where
            # `adb logcat | grep -i python` is the debugger.
            log.error("background task failed: %s\n%s", exc, traceback.format_exc())
            self.signals.failed.emit(exc)
        else:
            self.signals.done.emit(result)


#: Tasks that have been handed to the pool but have not finished.
#:
#: This is load-bearing, not bookkeeping. ``QThreadPool.start()`` takes
#: ownership of the runnable on the C++ side, but nothing keeps the *Python*
#: wrapper alive — and ``signals`` is an attribute of that wrapper. Once
#: ``run_in_background`` returns, the wrapper's last reference is gone and the
#: task can be collected before it ever runs, taking its signal connections with
#: it. The work then silently never happens: no exception, no log line, no
#: callback.
#:
#: It bit exactly that way — ``ChapelViewModel.refreshSchedule`` did nothing at
#: all, while two other callers happened to survive on timing. Holding a strong
#: reference until a terminal signal fires removes the luck.
_in_flight: set["_Task"] = set()
_in_flight_lock = threading.Lock()


def run_in_background(
    fn: Callable[[], object],
    on_done: Callable[[object], None],
    on_error: Callable[[BaseException], None],
) -> None:
    """Run ``fn`` on a pool thread; call back on the GUI thread.

    Both callbacks may be plain functions or closures — they do not have to be
    slots on a live QObject.
    """
    task = _Task(fn)

    with _in_flight_lock:
        _in_flight.add(task)

    def release(_: object = None) -> None:
        with _in_flight_lock:
            _in_flight.discard(task)

    task.signals.done.connect(on_done)
    task.signals.failed.connect(on_error)
    # Connected last so the caller's handler runs first and can still rely on
    # the task being alive.
    task.signals.done.connect(release)
    task.signals.failed.connect(release)

    QThreadPool.globalInstance().start(task)
