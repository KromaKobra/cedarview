"""Session metadata persistence.

Reminder of what this is *not*: it does not hold cookies or credentials. The
cookie jar belongs to the WebView. See the module docstring of
``mycu/core/session.py`` for why that is forced on us rather than chosen.
"""

from __future__ import annotations

import json
import os
import stat
from pathlib import Path

from mycu.core.session import SCHEMA_VERSION, SessionState, SessionStore, default_state_dir


def test_a_fresh_store_reports_no_session(tmp_path: Path) -> None:
    state = SessionStore(tmp_path).load()
    assert not state.has_logged_in
    assert state.last_login == 0.0


def test_round_trip(tmp_path: Path) -> None:
    store = SessionStore(tmp_path)
    state = store.load()
    state.last_term = "Fall 2026"
    store.mark_login(state)

    reloaded = SessionStore(tmp_path).load()
    assert reloaded.has_logged_in
    assert reloaded.last_term == "Fall 2026"


def test_state_file_is_not_world_readable(tmp_path: Path) -> None:
    store = SessionStore(tmp_path)
    store.save(store.load())
    mode = stat.S_IMODE(os.stat(store.path).st_mode)
    assert mode == 0o600


def test_a_corrupt_file_is_treated_as_no_session_not_a_crash(tmp_path: Path) -> None:
    # An app that cannot start without a terminal is worse than an app that
    # asks you to log in once more.
    store = SessionStore(tmp_path)
    store.ensure_dirs()
    store.path.write_text("{ this is not json")
    assert not store.load().has_logged_in


def test_an_older_schema_is_discarded(tmp_path: Path) -> None:
    store = SessionStore(tmp_path)
    store.ensure_dirs()
    store.path.write_text(json.dumps({"schema": SCHEMA_VERSION - 1, "last_login": 123.0}))
    assert not store.load().has_logged_in


def test_unknown_keys_from_a_future_version_are_ignored(tmp_path: Path) -> None:
    store = SessionStore(tmp_path)
    store.ensure_dirs()
    store.path.write_text(
        json.dumps({"schema": SCHEMA_VERSION, "last_login": 5.0, "something_new": True})
    )
    assert store.load().has_logged_in


def test_clear_forgets_everything(tmp_path: Path) -> None:
    store = SessionStore(tmp_path)
    store.mark_login(store.load())
    store.clear()
    assert not store.load().has_logged_in


def test_clear_is_safe_when_nothing_is_saved(tmp_path: Path) -> None:
    SessionStore(tmp_path).clear()


def test_profile_dir_sits_under_the_state_dir(tmp_path: Path) -> None:
    assert SessionStore(tmp_path).profile_dir().parent == tmp_path


def test_state_dir_honours_the_env_override(monkeypatch, tmp_path: Path) -> None:
    monkeypatch.setenv("MYCU_STATE_DIR", str(tmp_path / "elsewhere"))
    assert default_state_dir() == tmp_path / "elsewhere"


def test_android_state_lands_in_app_private_storage(monkeypatch, tmp_path: Path) -> None:
    monkeypatch.delenv("MYCU_STATE_DIR", raising=False)
    monkeypatch.setenv("ANDROID_ARGUMENT", str(tmp_path / "data"))
    assert default_state_dir() == tmp_path / "data" / "mycu"


def test_desktop_state_follows_xdg(monkeypatch, tmp_path: Path) -> None:
    monkeypatch.delenv("MYCU_STATE_DIR", raising=False)
    monkeypatch.delenv("ANDROID_ARGUMENT", raising=False)
    monkeypatch.delenv("ANDROID_PRIVATE", raising=False)
    monkeypatch.setenv("XDG_DATA_HOME", str(tmp_path / "xdg"))
    assert default_state_dir() == tmp_path / "xdg" / "mycu"


def test_state_defaults_are_all_falsey() -> None:
    state = SessionState()
    assert (state.last_login, state.last_success, state.last_expiry) == (0.0, 0.0, 0.0)
    assert state.schema == SCHEMA_VERSION
