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


# ---------------------------------------------------------------------------
# Expiry detection must key on the host, not the URL string
# ---------------------------------------------------------------------------

@pytest.mark.parametrize(
    "url",
    [
        # Observed for real in a `scripts/discover meals` capture: this raised
        # SessionExpired because the IdP name appears in a query parameter.
        "https://t.vibe.co/pixel/s?aid=X&url=https://selfservice.cedarville.edu/Cedarinfo/Meals"
        "&ref=https://login.microsoftonline.com/&ts=1789620230162",
        f"{BASE_URL}/Cedarinfo/Meals?returnUrl=https%3A%2F%2Flogin.microsoftonline.com%2F",
        "https://analytics.example.com/collect?dr=https://login.microsoftonline.com/",
    ],
)
def test_an_idp_in_a_query_parameter_is_not_an_expired_session(url: str) -> None:
    """A false positive here bounces the user to a sign-in they did not need."""
    assert not looks_like_login(url)


def test_a_lookalike_host_does_not_count() -> None:
    # Suffix matching must be on a dot boundary, or `evil-login.microsoftonline.com.attacker.tld`
    # style hosts would read as trusted IdPs.
    assert not looks_like_login("https://login.microsoftonline.com.example.net/saml2")
    assert not looks_like_login("https://notlogin.microsoftonline.com/saml2")


def test_a_real_idp_subdomain_still_counts() -> None:
    assert looks_like_login("https://eu.login.microsoftonline.com/x/saml2")


# ---------------------------------------------------------------------------
# TLS trust store
# ---------------------------------------------------------------------------

def test_ssl_context_has_certificate_authorities() -> None:
    """A context with no CAs verifies nothing.

    On desktop the system bundle supplies these. On Android nothing does —
    there is no OpenSSL default cert path in the app sandbox — and the symptom
    is CERTIFICATE_VERIFY_FAILED on every HTTPS request, which reads like a
    network fault. `ssl_context()` falls back to Android's own CA directory;
    this asserts the result is actually usable wherever the suite runs.
    """
    from mycu.core.transport import ssl_context

    assert ssl_context().cert_store_stats()["x509_ca"] > 0


def test_ssl_context_is_cached() -> None:
    """Building it per request would re-read the whole CA directory."""
    from mycu.core.transport import ssl_context

    assert ssl_context() is ssl_context()


def test_ssl_context_verifies_and_checks_hostnames() -> None:
    """The fallback must not quietly weaken verification."""
    import ssl as ssl_module

    from mycu.core.transport import ssl_context

    context = ssl_context()
    assert context.verify_mode == ssl_module.CERT_REQUIRED
    assert context.check_hostname is True


def test_android_ca_paths_are_absolute_directories() -> None:
    """Typos here degrade silently into 'no CAs found'."""
    from mycu.core.transport import ANDROID_CA_PATHS

    assert ANDROID_CA_PATHS
    assert all(p.startswith("/") for p in ANDROID_CA_PATHS)
    assert all("cacerts" in p for p in ANDROID_CA_PATHS)


def test_android_ca_fallback_actually_loads_certificates(tmp_path, monkeypatch) -> None:
    """Exercise the Android branch on a desktop, with a fake CA directory.

    This is the test that matters. The first attempt at this fallback used
    ``load_verify_locations(capath=...)``, which OpenSSL treats as a *lazy*
    lookup: nothing loads, ``cert_store_stats()`` keeps reporting zero, and
    verification still fails — but every test passed, because on a desktop the
    default context already has CAs and the fallback never ran. So build a
    directory in Android's shape and drive the fallback directly.
    """
    import re as _re
    import ssl as _ssl

    from mycu.core import transport

    # Android's files are a PEM block followed by an `openssl x509 -text` dump
    # and a fingerprint line. Reproduce that shape from the real system bundle
    # so the PEM extraction is tested against genuine certificates.
    bundle = Path(_ssl.get_default_verify_paths().cafile or "")
    if not bundle.is_file():
        pytest.skip("no system CA bundle to build a fixture from")

    blocks = _re.findall(
        r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----",
        bundle.read_text(errors="replace"),
        _re.DOTALL,
    )[:5]
    assert blocks, "system CA bundle held no PEM blocks"

    fake_store = tmp_path / "cacerts"
    fake_store.mkdir()
    for index, block in enumerate(blocks):
        # The trailing junk is the point: a naive reader would choke on it.
        (fake_store / f"{index:08x}.0").write_text(
            block
            + "\n-----\nCertificate:\n    Data:\n        Version: 3 (0x2)\n"
            + "SHA1 Fingerprint=AA:BB:CC\n"
        )

    monkeypatch.setattr(transport, "ANDROID_CA_PATHS", (str(fake_store),))

    empty = _ssl.SSLContext(_ssl.PROTOCOL_TLS_CLIENT)
    assert empty.cert_store_stats()["x509_ca"] == 0

    transport._load_android_cas(empty)
    assert empty.cert_store_stats()["x509_ca"] == len(blocks)


def test_android_ca_fallback_survives_a_junk_directory(tmp_path, monkeypatch) -> None:
    """A directory with nothing usable must log, not raise."""
    import ssl as _ssl

    from mycu.core import transport

    junk = tmp_path / "cacerts"
    junk.mkdir()
    (junk / "not-a-cert.0").write_text("this is not a certificate\n")

    monkeypatch.setattr(transport, "ANDROID_CA_PATHS", (str(junk),))

    empty = _ssl.SSLContext(_ssl.PROTOCOL_TLS_CLIENT)
    transport._load_android_cas(empty)          # must not raise
    assert empty.cert_store_stats()["x509_ca"] == 0
