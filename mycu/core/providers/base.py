"""The shape every provider shares."""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Generic, TypeVar

from ..transport import Response, Transport

T = TypeVar("T")


class Provider(ABC, Generic[T]):
    """Fetch one Self-Service path and turn it into domain objects.

    Subclasses set :attr:`path` and implement :meth:`parse`. :meth:`fetch` is
    shared and does the two things every provider must do identically:

    * check for an expired session **before** parsing, so a login page never
      reaches a parser and produces a confusing :class:`ParseError`;
    * leave everything else to the subclass.
    """

    #: Root-relative path on ``selfservice.cedarville.edu``.
    path: str = ""

    #: Human-readable name, used in error messages and the UI.
    label: str = ""

    def __init__(self, transport: Transport) -> None:
        self.transport = transport

    def fetch(self) -> T:
        """Fetch and parse. Raises :class:`~mycu.core.errors.SessionExpired`."""
        response = self.transport.get(self.path).raise_for_session()
        return self.parse(response)

    @abstractmethod
    def parse(self, response: Response) -> T:
        """Turn a response into domain objects.

        Must not perform I/O — this is the method the offline tests call
        directly with a fixture body.
        """
        raise NotImplementedError
