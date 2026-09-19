"""CedarView — a personal client for one student's own Cedarville records.

The app is CedarView; the Python package is still ``mycu``. That is deliberate
rather than unfinished: the package name is also the p4a dist name and half of
the Android applicationId (``org.mycu.mycu``), and to Android a new
applicationId is a different app — it installs beside the old one with an empty
data directory instead of over it. The import path is not worth that.

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
