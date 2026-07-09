# Copyright (c) 2016-2026 Memgraph Ltd. [https://memgraph.com]
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Client-side routing for Memgraph high-availability clusters.

This is a thin, Pythonic facade over libmgclient's routing engine, exposed by
the :mod:`mgclient._mgclient` C extension as ``_Router``. The engine -- routing
table fetch and caching (with TTL), read load-balancing, coordinator failover
and the managed-transaction retry loop -- lives in libmgclient and is shared
with every other driver built on it; this module only adapts it to an ergonomic
Python API.
"""

from mgclient._mgclient import _Router
from mgclient._mgclient import connect as _base_connect
from mgclient._mgclient import TransientError

#: Route the connection to a server that accepts writes (the cluster main).
ACCESS_MODE_WRITE = "WRITE"
#: Route the connection to a server that serves reads (a replica).
ACCESS_MODE_READ = "READ"

_ACCESS_MODES = (ACCESS_MODE_WRITE, ACCESS_MODE_READ)

_DEFAULT_MAX_RETRIES = 8
_DEFAULT_RETRY_BACKOFF = 1.0
_DEFAULT_RETRY_BACKOFF_CAP = 15.0


def is_transient_error(exc):
    """True for a transient HA condition worth retrying after a short backoff.

    These arise during a failover, while a replica catches up, or when an
    instance is dropped mid-request; they clear once the cluster reconverges.

    Classification is purely by type: the driver surfaces every transient
    condition as :exc:`mgclient.TransientError` (Memgraph's ``TransientError``
    Bolt code, or a low-level transport/connection failure). Errors Memgraph
    reports as ``ClientError`` -- including a write briefly forbidden while a new
    main is being elected -- are treated as non-transient, like any other
    client error.
    """
    return isinstance(exc, TransientError)


def _normalize_access_mode(access_mode):
    if isinstance(access_mode, str):
        access_mode = access_mode.upper()
    if access_mode not in _ACCESS_MODES:
        raise ValueError(
            f"access_mode must be one of {_ACCESS_MODES!r}, got {access_mode!r}"
        )
    return access_mode


class Router:
    """Client-side routing engine for a Memgraph high-availability cluster.

    A :class:`Router` is created against a seed coordinator and is meant to be
    long-lived and reused. It fetches the cluster routing table, caches it until
    its TTL expires, and hands out ordinary :class:`Connection` objects bound to
    the appropriate data instance.

    All connection parameters other than ``host``/``address``/``port`` (for
    example ``username``, ``password`` and the SSL options) are reused for both
    the coordinator and the data-instance connections.

    Parameters:

       * :obj:`host` / :obj:`address` / :obj:`port`

            The seed coordinator to contact for the first routing-table fetch.

       * :obj:`resolver`

            Optional callable mapping an advertised ``"host:port"`` address to
            an iterable of ``"host:port"`` targets to try, in order.  Defaults
            to using the advertised address unchanged.

       * :obj:`routing_context`

            Optional :class:`dict` forwarded to the coordinator's ``ROUTE``
            request.

       * :obj:`max_retries` / :obj:`retry_backoff` / :obj:`retry_backoff_cap`

            The managed-transaction retry budget (see :meth:`execute_read` and
            :meth:`execute_write`). Backoff is capped exponential:
            ``retry_backoff``, ``2 * retry_backoff``, ... up to
            ``retry_backoff_cap`` seconds.
    """

    def __init__(
        self,
        *,
        host=None,
        address=None,
        port=None,
        resolver=None,
        routing_context=None,
        max_retries=_DEFAULT_MAX_RETRIES,
        retry_backoff=_DEFAULT_RETRY_BACKOFF,
        retry_backoff_cap=_DEFAULT_RETRY_BACKOFF_CAP,
        **connect_kwargs,
    ):
        # Forward only the parameters that were actually given; the C _Router
        # applies its own defaults for anything omitted.
        params = {
            key: value
            for key, value in dict(
                connect_kwargs,
                host=host,
                address=address,
                port=port,
                resolver=resolver,
                routing_context=routing_context,
            ).items()
            if value is not None
        }
        self._router = _Router(
            max_retries=max_retries,
            retry_backoff=retry_backoff,
            retry_backoff_cap=retry_backoff_cap,
            **params,
        )

    def connect(self, access_mode=ACCESS_MODE_WRITE):
        """Open a connection to a data instance serving ``access_mode``.

        Returns an ordinary :class:`Connection`.  Raises
        :exc:`mgclient.TransientError` if no server for the requested access
        mode can be reached even after refreshing the routing table (a
        transient cluster condition, e.g. a failover in progress).
        """
        if _normalize_access_mode(access_mode) == ACCESS_MODE_WRITE:
            return self._router.connect_write()
        return self._router.connect_read()

    def execute_read(self, work):
        """Run ``work(cursor)`` as a managed read against a replica.

        ``work`` receives a :class:`Cursor` from a freshly routed READ
        connection and returns whatever the caller wants; that value is returned
        from :meth:`execute_read`.  On a transient cluster condition (see
        :func:`is_transient_error`) the routing table is refreshed and the work
        is retried with capped exponential backoff, up to ``max_retries``.

        ``work`` may be called more than once, so it should be free of side
        effects other than the database operations themselves.
        """
        return self._router.execute_read(work)

    def execute_write(self, work):
        """Run ``work(cursor)`` as a managed write against the main.

        Like :meth:`execute_read`, but the work runs inside an explicit
        transaction that is committed for you, and it is routed to the main.

        Transient failover conditions are retried.  A replication failure at
        commit is surfaced as an error like any other -- including a SYNC
        "committed on the main" failure, which is *not* treated as success: the
        write is durable only on that main, so if the main is then lost before
        an unreachable replica catches up the write is gone.  As with any
        retried write, make ``work`` idempotent (e.g. ``MERGE``) so a re-run
        after such an error cannot duplicate it.
        """
        return self._router.execute_write(work)

    def refresh(self):
        """Force an immediate refresh of the cached routing table."""
        self._router.refresh()

    @property
    def routing_table(self):
        """A snapshot of the (refreshed if absent) routing table as a dict with
        ``"ttl"``, ``"write"``, ``"read"`` and ``"route"`` entries."""
        return self._router.routing_table()


def connect(
    *,
    routing=False,
    access_mode=ACCESS_MODE_WRITE,
    resolver=None,
    routing_context=None,
    **kwargs,
):
    """Open a connection to Memgraph.

    Takes only keyword arguments, exactly like the C extension's
    :func:`connect`.

    With ``routing=False`` (the default) this is exactly the C extension's
    :func:`connect`: it opens a direct connection to the given host/port.

    With ``routing=True`` it treats the target as a coordinator of a
    high-availability cluster, fetches the routing table, and returns a
    connection to a data instance serving ``access_mode`` (``"WRITE"`` -> the
    main, ``"READ"`` -> a replica). A ``resolver`` may be supplied (see
    :class:`Router`) for environments where advertised addresses are not
    directly reachable. This is a one-shot convenience; construct a
    :class:`Router` directly to reuse the cached routing table across
    connections.
    """
    if not routing:
        return _base_connect(**kwargs)

    access_mode = _normalize_access_mode(access_mode)
    router = Router(
        resolver=resolver, routing_context=routing_context, **kwargs
    )
    return router.connect(access_mode=access_mode)
