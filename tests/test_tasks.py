"""Background task dispatch.

Small file, one real bug behind it.
"""

from __future__ import annotations

import gc

import pytest

pytest.importorskip("PySide6.QtCore", reason="PySide6 not available")

from PySide6.QtCore import QCoreApplication, QEventLoop, QThreadPool, QTimer  # noqa: E402

from mycu.ui.tasks import run_in_background  # noqa: E402


@pytest.fixture
def qapp():
    app = QCoreApplication.instance() or QCoreApplication([])
    yield app
    QThreadPool.globalInstance().waitForDone(5000)


def _pump(app, predicate, timeout_ms: int = 5000) -> None:
    """Spin the event loop until ``predicate`` or the timeout."""
    loop = QEventLoop()
    timer = QTimer()
    timer.setInterval(10)
    deadline = QTimer()
    deadline.setSingleShot(True)

    timer.timeout.connect(lambda: loop.quit() if predicate() else None)
    deadline.timeout.connect(loop.quit)
    timer.start()
    deadline.start(timeout_ms)
    loop.exec()


def test_a_result_reaches_the_callback(qapp) -> None:
    seen: list[object] = []
    run_in_background(lambda: 42, seen.append, lambda e: seen.append(e))
    _pump(qapp, lambda: bool(seen))
    assert seen == [42]


def test_an_exception_reaches_the_error_callback(qapp) -> None:
    errors: list[BaseException] = []
    run_in_background(
        lambda: (_ for _ in ()).throw(ValueError("boom")),
        lambda r: errors.append(r),
        errors.append,
    )
    _pump(qapp, lambda: bool(errors))
    assert isinstance(errors[0], ValueError)


def test_a_plain_closure_receiver_still_gets_called(qapp) -> None:
    """Regression: the task used to be collected before it ran.

    ``QThreadPool.start()`` owns the runnable on the C++ side, but nothing kept
    the Python wrapper — and its ``signals`` attribute — alive. With a bound
    method on a long-lived QObject as the receiver it happened to survive; with
    a local closure it did not, and the work silently never happened: no
    exception, no log line, no callback. `run_in_background` now holds a strong
    reference until a terminal signal fires.

    The explicit `gc.collect()` is the point of the test — it forces the
    collection that used to happen by chance.
    """
    seen: list[object] = []

    def start_and_drop_every_local_reference() -> None:
        def done(value: object) -> None:
            seen.append(value)

        def failed(exc: BaseException) -> None:
            seen.append(exc)

        run_in_background(lambda: "survived", done, failed)

    start_and_drop_every_local_reference()
    gc.collect()

    _pump(qapp, lambda: bool(seen))
    assert seen == ["survived"]


def test_many_concurrent_tasks_all_report(qapp) -> None:
    results: list[object] = []
    for n in range(12):
        run_in_background(lambda n=n: n * 2, results.append, results.append)

    _pump(qapp, lambda: len(results) >= 12)
    assert sorted(r for r in results if isinstance(r, int)) == [n * 2 for n in range(12)]


def test_finished_tasks_are_not_retained(qapp) -> None:
    """The strong reference must be released, or it is a leak."""
    from mycu.ui import tasks

    seen: list[object] = []
    run_in_background(lambda: 1, seen.append, seen.append)
    _pump(qapp, lambda: bool(seen))
    _pump(qapp, lambda: not tasks._in_flight, timeout_ms=2000)

    assert not tasks._in_flight
