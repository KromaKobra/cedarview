"""Transport protocol, URL resolution, and session-expiry detection.

Expiry detection is the part worth being paranoid about: a false negative sends
a login page to a parser (confusing error, no recovery), and a false positive
bounces you to a sign-in you did not need.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from mycu.core.errors import ParseError, SessionExpired
from mycu.core.transport import (
    BASE_URL,
    FixtureTransport,
    Response,
    Transport,
    is_selfservice,
    looks_like_login,
    resolve,
)


# ---------------------------------------------------------------------------
# URLs
# ---------------------------------------------------------------------------

@pytest.mark.parametrize(
    "path,expected",
    [
        ("/cedarinfo/chapelskip", f"{BASE_URL}/cedarinfo/chapelskip"),
        ("cedarinfo/chapelskip", f"{BASE_URL}/cedarinfo/chapelskip"),
        (f"{BASE_URL}/Student/Grades", f"{BASE_URL}/Student/Grades"),
        ("https://example.com/x", "https://example.com/x"),
    ],
)
def test_resolve(path: str, expected: str) -> None:
    assert resolve(path) == expected


def test_is_selfservice() -> None:
    assert is_selfservice(f"{BASE_URL}/cedarinfo/chapelskip")
    assert not is_selfservice("https://login.microsoftonline.com/x/saml2")


# ---------------------------------------------------------------------------
# Expiry detection
# ---------------------------------------------------------------------------

@pytest.mark.parametrize(
    "url",
    [
        "https://login.microsoftonline.com/81c32413-015d-4ba8-a93b-e1c28e355738/saml2?SAMLRequest=x",
        "https://login.microsoft.com/common/oauth2/authorize",
        "https://login.windows.net/common/",
        "https://LOGIN.MICROSOFTONLINE.COM/x",   # case must not matter
    ],
)
def test_idp_urls_are_detected(url: str) -> None:
    assert looks_like_login(url)


@pytest.mark.parametrize(
    "url",
    [
        f"{BASE_URL}/cedarinfo/chapelskip",
        f"{BASE_URL}/Student/Grades",
        "",
    ],
)
def test_selfservice_urls_are_not_mistaken_for_login(url: str) -> None:
    assert not looks_like_login(url)


def test_login_body_is_detected_even_on_the_selfservice_origin(login_html: str) -> None:
    """SAML posts the response back through the SP, so the URL can lie."""
    assert looks_like_login(f"{BASE_URL}/cedarinfo/chapelskip", login_html)


def test_a_short_body_is_not_treated_as_expiry() -> None:
    # Empty responses happen for unrelated reasons; guessing would send the user
    # to a sign-in page they did not need.
    assert not looks_like_login(f"{BASE_URL}/cedarinfo/chapelskip", "")
    assert not looks_like_login(f"{BASE_URL}/cedarinfo/chapelskip", "{}")


def test_real_data_is_not_mistaken_for_a_login_page(chapel_html: str) -> None:
    assert not looks_like_login(f"{BASE_URL}/cedarinfo/chapelskip", chapel_html)


# ---------------------------------------------------------------------------
# Response
# ---------------------------------------------------------------------------

def test_raise_for_session_lets_good_responses_through(chapel_html: str) -> None:
    response = Response(200, f"{BASE_URL}/cedarinfo/chapelskip", chapel_html)
    assert response.raise_for_session() is response


def test_raise_for_session_catches_a_redirect_to_the_idp(login_html: str) -> None:
    response = Response(200, "https://login.microsoftonline.com/x/saml2", login_html)
    with pytest.raises(SessionExpired):
        response.raise_for_session()


def test_json_error_includes_the_body_so_you_can_see_it_was_html() -> None:
    response = Response(200, f"{BASE_URL}/x", "<!DOCTYPE html><html>…")
    with pytest.raises(ParseError, match="DOCTYPE"):
        response.json()


def test_ok_reflects_the_status_code() -> None:
    assert Response(200, "u", "").ok
    assert Response(204, "u", "").ok
    assert not Response(302, "u", "").ok
    assert not Response(500, "u", "").ok


# ---------------------------------------------------------------------------
# FixtureTransport
# ---------------------------------------------------------------------------

def test_fixture_transport_satisfies_the_protocol(fixtures_dir: Path) -> None:
    assert isinstance(FixtureTransport(fixtures_dir), Transport)


@pytest.mark.parametrize(
    "path,slug",
    [
        ("/cedarinfo/chapelskip", "cedarinfo_chapelskip"),
        ("cedarinfo/chapelskip", "cedarinfo_chapelskip"),
        (f"{BASE_URL}/cedarinfo/chapelskip?term=FA26", "cedarinfo_chapelskip"),
        ("/Student/Grades", "student_grades"),
    ],
)
def test_fixture_slugs(path: str, slug: str) -> None:
    assert FixtureTransport.slug(path) == slug


def test_fixture_transport_serves_the_chapel_page(fixtures_dir: Path) -> None:
    response = FixtureTransport(fixtures_dir).get("/cedarinfo/chapelskip")
    assert response.ok
    assert "Chapel Attendance" in response.body
    assert response.url == f"{BASE_URL}/cedarinfo/chapelskip"


def test_fixture_transport_names_what_it_looked_for(fixtures_dir: Path) -> None:
    # A missing fixture is a routine thing to hit while adding a provider; the
    # error should say what to create, not just "not found".
    with pytest.raises(FileNotFoundError, match="student_grades"):
        FixtureTransport(fixtures_dir).get("/Student/Grades")


def test_json_fixture_wins_over_html(tmp_path: Path) -> None:
    """This is how the app switches to a JSON payload without a code change."""
    (tmp_path / "cedarinfo_chapelskip.html").write_text("<html>html</html>")
    (tmp_path / "cedarinfo_chapelskip.json").write_text('{"from": "json"}')

    response = FixtureTransport(tmp_path).get("/cedarinfo/chapelskip")
    assert response.json() == {"from": "json"}
