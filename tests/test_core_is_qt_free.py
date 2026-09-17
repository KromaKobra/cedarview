"""The architectural rule, enforced.

``mycu.core`` must not import PySide6 — not at module scope, not lazily inside a
function. This is the seam that makes the Android port a packaging exercise
instead of a rewrite, and it is the reason every parsing test above runs with no
display and no network. It is also exactly the kind of rule that erodes silently
unless something fails when it is broken.
"""

from __future__ import annotations

import ast
import subprocess
import sys
from pathlib import Path

import pytest

CORE = Path(__file__).resolve().parents[1] / "mycu" / "core"
FORBIDDEN = ("PySide6", "shiboken6", "PyQt5", "PyQt6")


def core_modules() -> list[Path]:
    return sorted(CORE.rglob("*.py"))


def test_there_is_something_to_check() -> None:
    assert core_modules(), "no core modules found — has the layout moved?"


@pytest.mark.parametrize("path", core_modules(), ids=lambda p: p.name)
def test_no_qt_import_anywhere_in_the_file(path: Path) -> None:
    """Walk the AST, so a lazy import inside a function is caught too."""
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))

    offenders = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            offenders += [a.name for a in node.names if a.name.split(".")[0] in FORBIDDEN]
        elif isinstance(node, ast.ImportFrom) and node.module:
            if node.module.split(".")[0] in FORBIDDEN:
                offenders.append(node.module)

    assert not offenders, (
        f"{path.relative_to(CORE.parents[1])} imports {offenders}. "
        "mycu.core must stay Qt-free — move this into mycu.ui."
    )


def test_core_imports_in_a_subprocess_with_pyside6_unavailable() -> None:
    """Belt and braces: import the core with PySide6 blocked outright.

    The AST check cannot see an import hidden behind ``importlib`` or a
    ``__import__`` call. This one cannot be fooled.
    """
    script = """
import sys

class Blocker:
    def find_module(self, name, path=None):
        if name.split('.')[0] in ('PySide6', 'shiboken6'):
            raise ImportError('PySide6 is deliberately unavailable in this check')
        return None

    def find_spec(self, name, path=None, target=None):
        return self.find_module(name, path)

sys.meta_path.insert(0, Blocker())

import mycu.core.models
import mycu.core.session
import mycu.core.transport
import mycu.core.errors
import mycu.core.providers.chapel
print('ok')
"""
    result = subprocess.run(
        [sys.executable, "-c", script],
        capture_output=True,
        text=True,
        cwd=str(CORE.parents[1]),
    )
    assert result.returncode == 0, result.stderr
    assert "ok" in result.stdout


def test_the_platform_package_does_not_shadow_the_stdlib() -> None:
    """``mycu.platform`` must not become the stdlib's ``platform``.

    A top-level ``platform/`` directory on ``sys.path`` would shadow the stdlib
    module that Qt, setuptools and python-for-android all import — a failure
    that shows up far from its cause. Nesting it under ``mycu`` prevents that;
    this asserts the nesting has not been undone.
    """
    import platform as stdlib_platform

    import mycu.platform as ours

    assert stdlib_platform.__name__ == "platform"
    assert hasattr(stdlib_platform, "machine")
    assert ours.__name__ == "mycu.platform"
    assert ours is not stdlib_platform
