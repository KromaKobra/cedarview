"""``python -m mycu`` -> the Qt app.

Kept as a one-line shim so the heavy PySide6 import stays inside
:mod:`mycu.ui.app` and the core package remains importable without Qt.
"""

from .ui.app import main

if __name__ == "__main__":
    raise SystemExit(main())
