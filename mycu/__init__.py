"""myCU — a personal client for one student's own Cedarville records.

Layout (see docs/architecture.md for the reasoning):

    mycu.core       Pure Python. Never imports PySide6. Testable offline.
    mycu.ui         Qt/QML layer: login WebView, transport, viewmodels.
    mycu.platform   The only place that knows whether we're on desktop or Android.

The two seams that matter:

1. ``mycu.core`` has no Qt dependency at all, enforced by
   ``tests/test_core_is_qt_free.py``. The whole parsing layer runs under pytest
   with no display and no network.
2. Nothing above :class:`mycu.core.transport.Transport` knows *how* a request
   was made. On both desktop and Android it is a ``fetch()`` evaluated inside a
   logged-in WebView page, but a provider cannot tell.
"""

__version__ = "0.1.0"
