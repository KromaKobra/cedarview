"""A very small HTML tree, built on :mod:`html.parser` from the standard library.

.. rubric:: Why this exists

The meal-plan page (:mod:`mycu.core.providers.meals`) is the only server-rendered
HTML this app parses; everything else is JSON. That one page used to pull in
``lxml``, which is a **C extension** — and a C extension is the single most
expensive kind of dependency to put in an Android APK, because
python-for-android needs a cross-compilation recipe for it (libxml2 and libxslt
have to be built for the target ABI). Trading a 6 MB binary wheel for ~80 lines
of stdlib removes that from the build entirely. See ``docs/android.md``.

.. rubric:: What it deliberately is not

This is **not** a spec-compliant HTML5 parser, and it must not grow into one.
It implements exactly the four operations ``meals.py`` needs:

* find every ``<p>`` in document order
* find the first ``<strong>``/``<b>`` inside an element
* read an element's concatenated text
* find ``<h5>`` inside a ``<fieldset>``

Text is concatenated with no separator inserted between nodes, matching
``lxml``'s ``text_content()`` — the meal-plan sentences rely on the source's own
spacing, so adding separators would change what the markers match against.
"""

from __future__ import annotations

from html.parser import HTMLParser
from typing import Iterator

__all__ = ["Element", "parse"]

#: Elements that never have children or an end tag.
VOID = frozenset({
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr",
})

#: An open ``<p>`` is closed implicitly by any of these, per HTML5. The real
#: page closes its paragraphs properly, but Cedarville's markup is hand-written
#: and a stray unclosed ``<p>`` would otherwise swallow the rest of the
#: fieldset into one element — which would merge all three balances into a
#: single "paragraph" and break the phrase matching.
CLOSES_P = frozenset({
    "address", "article", "aside", "blockquote", "details", "div", "dl",
    "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2", "h3",
    "h4", "h5", "h6", "header", "hgroup", "hr", "main", "menu", "nav", "ol",
    "p", "pre", "section", "table", "ul",
})

#: Text inside these is code, not content, and never counts towards
#: ``text_content()``. The trimmed fixture has neither, but the live 35 KB page
#: is full of both.
NON_TEXT = frozenset({"script", "style"})


class Element:
    """One node. Children are ``Element`` objects and ``str`` text runs."""

    __slots__ = ("tag", "attrs", "children", "parent")

    def __init__(self, tag: str, attrs: dict[str, str] | None = None) -> None:
        self.tag = tag
        self.attrs: dict[str, str] = attrs or {}
        self.children: list[Element | str] = []
        self.parent: Element | None = None

    def append(self, node: Element | str) -> None:
        if isinstance(node, Element):
            node.parent = self
        self.children.append(node)

    def iter_descendants(self) -> Iterator[Element]:
        """Every descendant element, document order, excluding ``self``."""
        for child in self.children:
            if isinstance(child, Element):
                yield child
                yield from child.iter_descendants()

    def find_all(self, *tags: str) -> list[Element]:
        """Descendants whose tag is any of ``tags``, in document order.

        Passing several tags matches an XPath union such as
        ``.//strong | .//b``, which ``lxml`` also returns in document order —
        load-bearing for "the *first* ``<strong>``" in ``meals.py``.
        """
        wanted = frozenset(tags)
        return [el for el in self.iter_descendants() if el.tag in wanted]

    def text_content(self) -> str:
        """All text in this subtree, concatenated, no separators added."""
        if self.tag in NON_TEXT:
            return ""
        out: list[str] = []
        for child in self.children:
            if isinstance(child, str):
                out.append(child)
            else:
                out.append(child.text_content())
        return "".join(out)

    def __repr__(self) -> str:  # pragma: no cover - debugging aid
        return f"<Element {self.tag} children={len(self.children)}>"


class _Builder(HTMLParser):
    """Turns a byte-soup page into an :class:`Element` tree.

    ``convert_charrefs=True`` (the default) resolves ``&amp;``/``&nbsp;`` into
    text for us, so entity handling is not this class's problem.
    """

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.root = Element("[document]")
        self._stack: list[Element] = [self.root]

    @property
    def _open(self) -> Element:
        return self._stack[-1]

    def _close_through(self, tag: str) -> None:
        """Pop up to and including the nearest open ``tag``.

        An end tag with no matching open element is ignored rather than
        unwinding the stack — stray ``</div>`` is common in real pages and
        must not tear down the fieldset around it.
        """
        for depth in range(len(self._stack) - 1, 0, -1):
            if self._stack[depth].tag == tag:
                del self._stack[depth:]
                return

    def _has_open(self, tag: str) -> bool:
        return any(el.tag == tag for el in self._stack[1:])

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        # Close an open <p> even when it is not the innermost element: markup
        # like `<p>text <span>more` followed by a `<div>` leaves <span> on top,
        # and checking only the innermost tag would miss it. HTML5 forbids any
        # of CLOSES_P inside a paragraph, so unwinding to the <p> is right.
        if tag in CLOSES_P and self._has_open("p"):
            self._close_through("p")

        element = Element(tag, {k: (v or "") for k, v in attrs})
        self._open.append(element)
        if tag not in VOID:
            self._stack.append(element)

    def handle_startendtag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        # <br/> style. Never opens a scope.
        self._open.append(Element(tag, {k: (v or "") for k, v in attrs}))

    def handle_endtag(self, tag: str) -> None:
        if tag not in VOID:
            self._close_through(tag)

    def handle_data(self, data: str) -> None:
        self._open.append(data)


def parse(body: str) -> Element:
    """Parse ``body`` into a tree and return its root.

    Never raises on malformed markup — a page this cannot make sense of yields
    a tree with no matching elements, which callers surface as a ``ParseError``
    with a useful message instead of a parser traceback.
    """
    builder = _Builder()
    builder.feed(body)
    builder.close()
    return builder.root
