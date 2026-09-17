"""Platform detection and web-backend selection.

This package is the *entire* Android-specific surface of the app. Everything
else — core, viewmodels, QML — is shared verbatim.

.. note::
   The package is ``mycu.platform``, not a top-level ``platform/`` directory.
   That is deliberate: a top-level ``platform/`` on ``sys.path`` would shadow
   the **standard library** ``platform`` module, which Qt, python-for-android
   and setuptools all import. Nesting it under ``mycu`` keeps ``import
   platform`` resolving to the stdlib everywhere (Python 3 absolute imports).

Two backends implement the same small interface:

===============  ===============================  ==========================
Backend          Web engine                       Initialisation order
===============  ===============================  ==========================
``desktop``      QtWebEngine (Chromium, in-proc)   *before* QGuiApplication
``android``      QtWebView (the system WebView)    *after* QGuiApplication,
                                                   before the QML engine
===============  ===============================  ==========================

The ordering difference is why :class:`~mycu.platform.base.WebBackend` has both
:meth:`before_app` and :meth:`after_app` hooks rather than one ``initialize``.
QtWebEngine does not exist on Android at all; QtWebView is its substitute, and
its QML ``WebView`` type has the two methods the transport actually needs —
``url`` and ``runJavaScript`` — which is the whole reason this port is cheap.
"""

from __future__ import annotations

import os
import sys

from .base import WebBackend

__all__ = ["WebBackend", "is_android", "current_backend", "backend_name"]


def is_android() -> bool:
    """Whether we are running inside an Android app process.

    ``ANDROID_ARGUMENT`` is set by python-for-android in every app process;
    ``sys.platform`` does not distinguish Android from Linux. ``MYCU_PLATFORM``
    overrides for testing the selection logic on a workstation (it will not make
    QtWebView appear on a desktop build — it only exercises the branch).
    """
    override = os.environ.get("MYCU_PLATFORM", "").strip().lower()
    if override:
        return override == "android"
    return bool(os.environ.get("ANDROID_ARGUMENT")) or "ANDROID_BOOTLOGO" in os.environ


def backend_name() -> str:
    return "android" if is_android() else "desktop"


def current_backend() -> WebBackend:
    """Return the backend for this platform.

    Imported lazily and by name so that a desktop run never touches
    ``PySide6.QtWebView`` (absent from the desktop wheel) and an Android run
    never touches ``PySide6.QtWebEngineQuick`` (absent from the Android build).
    An unconditional ``import`` of either at module scope would break the other
    platform at startup.
    """
    if is_android():
        from .android import AndroidBackend

        return AndroidBackend()

    from .desktop import DesktopBackend

    return DesktopBackend()


def describe() -> str:
    """One-line environment summary, logged at startup and shown in About."""
    return (
        f"{backend_name()} / python {sys.version.split()[0]} / "
        f"{sys.platform}"
    )
