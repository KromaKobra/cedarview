"""Qt <-> QML bridge.

A viewmodel holds one screen's worth of state as Qt properties, runs its
provider on a worker thread, and translates core exceptions into things QML can
display. It contains no parsing and no HTTP.

The pattern for a new screen is always the same:

* a ``QAbstractListModel`` for the rows (QML list views want a model, not a
  property holding a list — a property re-emits the whole list on every change);
* a ``QObject`` with a ``refresh()`` slot, ``busy``/``error`` properties, and
  whatever scalars the header shows.
"""

from .chapel import ChapelListModel, ChapelViewModel

__all__ = ["ChapelListModel", "ChapelViewModel"]
