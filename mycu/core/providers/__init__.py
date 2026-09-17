"""Providers: one module per thing you want to see.

A provider takes a :class:`~mycu.core.transport.Transport`, fetches one path,
and returns domain objects. It knows nothing about Qt, cookies, logins or
Android. That is what makes "much more in future releases" cheap — grades,
schedule and student account are each one more module here, sharing the session
and transport untouched.

Adding one:

1. Capture the page during an authenticated session (see ``docs/discovery.md``).
2. Drop the scrubbed body in ``tests/fixtures/<slug>.json`` or ``.html``.
3. Write ``mycu/core/providers/<name>.py`` subclassing :class:`Provider`.
4. Write ``tests/test_<name>_provider.py`` against the fixture.
5. Expose it with a viewmodel + a QML page.

Steps 1–4 need no phone, no display and no network.

A provider may live on any origin. ``chapel`` is on Self-Service and needs the
SAML session; ``dining`` is on ``diningdata.cedarville.edu`` and needs no auth
at all. :class:`~mycu.core.transport.TransportRouter` picks the right transport
from the origin, so neither provider has to care.
"""

from .base import Provider
from .chapel import ChapelProvider
from .chapel_schedule import ChapelScheduleProvider
from .dining import DiningProvider
from .meals import MealsProvider

__all__ = [
    "Provider",
    "ChapelProvider",
    "ChapelScheduleProvider",
    "DiningProvider",
    "MealsProvider",
]
