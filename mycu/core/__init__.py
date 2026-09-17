"""Pure-Python core. No ``import PySide6`` anywhere below this package.

This rule is not stylistic. It is what makes the Android port a packaging
exercise instead of a rewrite, and what lets the parsing tests run headless with
no network. ``tests/test_core_is_qt_free.py`` fails the build if it is broken.
"""
