"""Android entry point.

``pyside6-android-deploy`` requires the application's entry point to be a
top-level module named exactly ``main.py`` — python-for-android's bootstrap
imports it by name. It is not used on desktop, where ``python -m mycu`` and the
``mycu`` console script (both of which reach :func:`mycu.ui.app.main`) remain
the way in.

Two things differ from the desktop entry points, and both are deliberate:

1. **Arguments are not parsed.** ``sys.argv`` on Android is whatever the
   bootstrap happened to leave there, not a user's command line. Passing it to
   ``argparse`` risks ``SystemExit(2)`` on startup — which on a phone looks
   like the app silently failing to open, with the reason buried in logcat.
   ``main([])`` pins the real, non-demo configuration.

2. **Startup failures are logged before they propagate.** An uncaught exception
   here is invisible on a device; ``python-for-android`` tags *stdout* with the
   app name, so printing the traceback there is what makes
   ``adb logcat | grep -i mycu`` useful. See docs/android.md.
"""

from __future__ import annotations

import sys
import traceback


def main() -> int:
    try:
        from mycu.ui.app import main as app_main
    except ImportError:
        # Almost always a Qt module that was pruned out of the APK — QtWebView
        # is the usual casualty, and without it there is no login at all.
        print("mycu: FATAL — could not import the application", file=sys.stdout)
        traceback.print_exc(file=sys.stdout)
        sys.stdout.flush()
        raise

    try:
        return app_main([])
    except Exception:
        print("mycu: FATAL — unhandled exception at startup", file=sys.stdout)
        traceback.print_exc(file=sys.stdout)
        sys.stdout.flush()
        raise


if __name__ == "__main__":
    raise SystemExit(main())
