"""Exception types shared by the core and the Qt layer.

Kept in the core (and therefore Qt-free) so that providers can raise them and
viewmodels can catch them without either side importing the other's world.
"""

from __future__ import annotations


class MycuError(Exception):
    """Base class for every error this app raises deliberately."""


class SessionExpired(MycuError):
    """The Ellucian session is no longer valid; the user must log in again.

    This is *detected, never predicted*. We do not track cookie lifetimes or
    guess at Entra ID's session policy — we make the request, look at where we
    landed, and if it looks like an identity provider instead of Self-Service,
    we raise this. The viewmodel's response is always the same: reopen the
    login WebView, then retry the request once.

    See :func:`mycu.core.transport.looks_like_login` for the detection rules.
    """


class TransportError(MycuError):
    """The request could not be completed for a reason other than auth.

    Network down, WebView not ready, JavaScript threw, timeout waiting for the
    in-page fetch to deposit its result. Distinct from :class:`SessionExpired`
    because the remedy is different: retry or surface the error, do not bounce
    the user to a login page.
    """


class ParseError(MycuError):
    """The response arrived but did not have the shape we expect.

    Almost always means Ellucian changed the page. ``scripts/check-live`` exists
    to tell you that quickly instead of leaving you guessing.
    """
