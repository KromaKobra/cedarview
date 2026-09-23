"""Backend selection, and the QML contract between the two web surfaces.

None of this needs a phone. It checks the decisions that are made *before* any
device-specific code runs, plus the one invariant that would otherwise only
break on-device: the two surfaces must expose the same interface.
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest

from mycu import platform as mycu_platform

QML_DIR = Path(__file__).resolve().parents[1] / "mycu" / "ui" / "qml"

#: Everything mycu/ui/transport_webview.py and Main.qml call on a surface.
SURFACE_CONTRACT = ("currentUrl", "evalResult", "evalAsync", "navigate")

SURFACES = ["WebSurfaceDesktop.qml", "WebSurfaceAndroid.qml", "WebSurfaceStub.qml"]


def test_desktop_is_the_default(monkeypatch) -> None:
    monkeypatch.delenv("MYCU_PLATFORM", raising=False)
    monkeypatch.delenv("ANDROID_ARGUMENT", raising=False)
    monkeypatch.delenv("ANDROID_BOOTLOGO", raising=False)
    assert not mycu_platform.is_android()
    assert mycu_platform.backend_name() == "desktop"


def test_android_argument_selects_android(monkeypatch) -> None:
    monkeypatch.delenv("MYCU_PLATFORM", raising=False)
    monkeypatch.setenv("ANDROID_ARGUMENT", "/data/data/org.mycu/files")
    assert mycu_platform.is_android()


def test_the_override_wins_both_ways(monkeypatch) -> None:
    monkeypatch.setenv("ANDROID_ARGUMENT", "/data/data/org.mycu/files")
    monkeypatch.setenv("MYCU_PLATFORM", "desktop")
    assert not mycu_platform.is_android()

    monkeypatch.delenv("ANDROID_ARGUMENT", raising=False)
    monkeypatch.setenv("MYCU_PLATFORM", "android")
    assert mycu_platform.is_android()


def test_selecting_the_backend_does_not_import_the_other_platforms_module(monkeypatch) -> None:
    """The lazy import in ``current_backend`` is load-bearing.

    ``PySide6.QtWebView`` does not exist in the desktop wheel and
    ``PySide6.QtWebEngineQuick`` does not exist in the Android build. An
    unconditional import of either at module scope breaks the other platform at
    startup — a failure you would only discover on the device.
    """
    import importlib
    import sys

    monkeypatch.delenv("MYCU_PLATFORM", raising=False)
    monkeypatch.delenv("ANDROID_ARGUMENT", raising=False)
    sys.modules.pop("mycu.platform.android", None)

    importlib.reload(mycu_platform)
    assert "mycu.platform.android" not in sys.modules


def test_backends_declare_a_surface_and_a_name() -> None:
    from mycu.platform.android import AndroidBackend
    from mycu.platform.desktop import DesktopBackend

    for backend in (DesktopBackend(), AndroidBackend()):
        assert backend.name in ("desktop", "android")
        assert backend.surface_qml.endswith(".qml")
        assert (QML_DIR / backend.surface_qml).exists()


def test_the_init_hooks_run_in_opposite_orders() -> None:
    """Documented, and asserted, because getting it wrong crashes rather than raises.

    QtWebEngine must initialise *before* QGuiApplication; QtWebView *after* it.
    Each backend therefore implements exactly one of the two hooks, and
    ``mycu/ui/app.py`` calls both in the right places.
    """
    import inspect

    from mycu.platform.android import AndroidBackend
    from mycu.platform.desktop import DesktopBackend

    desktop_before = inspect.getsource(DesktopBackend.before_app)
    android_after = inspect.getsource(AndroidBackend.after_app)

    assert "QtWebEngineQuick" in desktop_before
    assert "QtWebView" in android_after


@pytest.mark.parametrize("name", SURFACES)
def test_every_surface_honours_the_same_contract(name: str) -> None:
    source = (QML_DIR / name).read_text(encoding="utf-8")
    for member in SURFACE_CONTRACT:
        assert member in source, f"{name} is missing '{member}'"


def test_the_android_surface_takes_its_url_from_the_load_request() -> None:
    """Regression, found on the device: sign-in was impossible on Android.

    QtWebView's ``url`` property is the URL that was *requested*, not the one
    the browser ended up on, so a server-side redirect — which is exactly how
    the SAML sign-in begins — never updates it. ``currentUrl`` was bound to it,
    the login state machine never saw ``login.microsoftonline.com``, the
    sign-in surface never opened, and the app showed a CORS error with no way
    to authenticate.

    ``loadRequest.url`` is the only place the post-redirect URL appears, so
    assert the handler reads it. Asserted against the source because the bug
    is in QML wiring, which no headless test can execute.
    """
    # Comments stripped first: the file explains the old binding by quoting it,
    # and a prose mention of `view.url` is not a live binding to it.
    source = re.sub(
        r"//[^\n]*", "", (QML_DIR / "WebSurfaceAndroid.qml").read_text(encoding="utf-8")
    )

    handler = re.search(
        r"onLoadingChanged\s*:\s*function\s*\((\w+)\)\s*\{(.*?)\n        \}",
        source,
        re.DOTALL,
    )
    assert handler, "WebSurfaceAndroid.qml has no onLoadingChanged handler"

    param, body = handler.group(1), handler.group(2)
    assert f"{param}.url" in body, (
        "onLoadingChanged must set currentUrl from the load request's url; "
        "without it a redirect to the identity provider is invisible to Python "
        "and interactive sign-in cannot start."
    )
    assert "currentUrl" in body, "the load request's url must reach currentUrl"

    assert not re.search(r"property\s+string\s+currentUrl\s*:\s*view\.url", source), (
        "currentUrl must not be bound to view.url — on QtWebView that property "
        "does not follow redirects. Assign it from onLoadingChanged instead."
    )


@pytest.mark.parametrize("name", SURFACES)
def test_surfaces_deliver_results_through_the_tagged_signal(name: str) -> None:
    """The transport tags each request; results must come back tagged.

    Without the token the poller cannot tell two in-flight requests apart.
    """
    source = (QML_DIR / name).read_text(encoding="utf-8")
    assert re.search(r"signal\s+evalResult\s*\(\s*string\s+token", source), name


def qml_imports(name: str) -> list[str]:
    """The modules a QML file imports. Comments mentioning them do not count."""
    source = (QML_DIR / name).read_text(encoding="utf-8")
    return re.findall(r"^\s*import\s+([\w.]+)", source, re.MULTILINE)


def test_the_stub_surface_imports_nothing_platform_specific() -> None:
    """``--demo`` must not need QtWebEngine — that is the whole point of it."""
    assert qml_imports("WebSurfaceStub.qml") == ["QtQuick"]


def test_each_real_surface_imports_only_its_own_backend() -> None:
    """Importing the wrong one fails at QML load time, on the device.

    QtWebEngine does not exist on Android and QtWebView is not in the desktop
    wheel, so a stray import is a startup crash on whichever platform is not
    the one you tested.
    """
    desktop = qml_imports("WebSurfaceDesktop.qml")
    android = qml_imports("WebSurfaceAndroid.qml")

    assert "QtWebEngine" in desktop and "QtWebView" not in desktop
    assert "QtWebView" in android and "QtWebEngine" not in android


def test_the_shared_qml_never_imports_a_web_module() -> None:
    """Main.qml and ChapelView.qml ship verbatim to both platforms."""
    for name in ("Main.qml", "ChapelView.qml"):
        imports = qml_imports(name)
        assert not any(i.startswith("QtWeb") for i in imports), f"{name}: {imports}"


def test_describe_mentions_the_backend() -> None:
    assert mycu_platform.backend_name() in mycu_platform.describe()


def test_desktop_profile_actually_persists(tmp_path: Path) -> None:
    """The login must survive a relaunch, or every test run costs an MFA prompt.

    Qt 6's default profile is off-the-record and silently ignores a storage
    path, so this asserts on what Qt reports back rather than on what we set.
    Runs in a subprocess because QtWebEngine must initialise before any
    QGuiApplication, and other tests may already have made one.
    """
    import subprocess
    import sys

    script = f"""
from pathlib import Path
from PySide6.QtGui import QGuiApplication
from mycu.platform.desktop import DesktopBackend
backend = DesktopBackend()
backend.before_app()
app = QGuiApplication([])
backend.configure_profile(Path({str(tmp_path)!r}))
p = backend.qml_profile()
print(p.isOffTheRecord(), p.persistentCookiesPolicy().name, p.persistentStoragePath())
"""
    out = subprocess.run(
        [sys.executable, "-c", script], capture_output=True, text=True, timeout=60
    )
    assert out.returncode == 0, out.stderr
    assert out.stdout.split()[-3:] == ["False", "ForcePersistentCookies", str(tmp_path)]
