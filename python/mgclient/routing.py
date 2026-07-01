# Copyright (c) 2016-2020 Memgraph Ltd. [https://memgraph.com]
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

This module builds a small routing layer on top of the primitives provided by
the :mod:`mgclient._mgclient` C extension (:func:`connect` and
:meth:`Connection.get_routing_table`).  It is pure Python and opens no
connections beyond the ones the C extension already makes.

The :class:`Router` class is the reusable engine: it caches the routing table
(honouring its TTL), balances reads across replicas, and fails over across
coordinators when refreshing.  :func:`connect` with ``routing=True`` is a
one-shot convenience built on top of it.
"""

import threading
import time

from mgclient._mgclient import connect as _base_connect
from mgclient._mgclient import OperationalError

#: Route the connection to a server that accepts writes (the cluster main).
ACCESS_MODE_WRITE = "WRITE"
#: Route the connection to a server that serves reads (a replica).
ACCESS_MODE_READ = "READ"

_ACCESS_MODES = (ACCESS_MODE_WRITE, ACCESS_MODE_READ)


def _default_resolver(address):
    """Identity resolver: the advertised address is used as-is."""
    return [address]


def _normalize_access_mode(access_mode):
    if isinstance(access_mode, str):
        access_mode = access_mode.upper()
    if access_mode not in _ACCESS_MODES:
        raise ValueError(
            f"access_mode must be one of {_ACCESS_MODES!r}, got {access_mode!r}"
        )
    return access_mode


def _split_host_port(address):
    """Split a routing-table ``"host:port"`` address into ``(host, port)``."""
    host, separator, port = address.rpartition(":")
    if not separator or not host:
        raise ValueError(f"invalid server address in routing table: {address!r}")
    try:
        port = int(port)
    except ValueError:
        raise ValueError(
            f"invalid port in routing table address: {address!r}"
        ) from None
    return host, port


class RoutingTable:
    """A parsed routing table: advertised addresses grouped by role.

    Addresses are the raw ``"host:port"`` strings advertised by the
    coordinator, before any :class:`Router` ``resolver`` is applied.
    """

    def __init__(self, write, read, route, ttl):
        self.write = list(write)
        self.read = list(read)
        self.route = list(route)
        self.ttl = ttl

    @classmethod
    def parse(cls, raw):
        """Build a :class:`RoutingTable` from a ``get_routing_table`` result."""
        buckets = {
            ACCESS_MODE_WRITE: [],
            ACCESS_MODE_READ: [],
            "ROUTE": [],
        }
        for server in raw.get("servers", []):
            role = server.get("role")
            if role in buckets:
                buckets[role].extend(server.get("addresses", []))
        return cls(
            buckets[ACCESS_MODE_WRITE],
            buckets[ACCESS_MODE_READ],
            buckets["ROUTE"],
            int(raw.get("ttl", 0)),
        )

    def servers(self, access_mode):
        """Return the advertised addresses serving ``access_mode``."""
        if access_mode == ACCESS_MODE_WRITE:
            return self.write
        return self.read


class Router:
    """Routing engine for a Memgraph high-availability cluster.

    A :class:`Router` is created against one or more coordinators and is meant
    to be long-lived and reused.  It fetches the cluster routing table via a
    Bolt ``ROUTE`` message and caches it until its TTL expires; :meth:`connect`
    then hands out ordinary :class:`Connection` objects bound to the
    appropriate data instance.

    Reads are balanced across the available replicas in round-robin order, and
    both the ``ROUTE`` refresh (across coordinators) and the data-instance
    connect (across a role's servers) fail over across candidates.  If every
    candidate for a requested access mode is unreachable, the cached table is
    refreshed and the attempt retried once before giving up.

    All connection parameters other than ``host``/``port``/``address`` (for
    example ``username``, ``password`` and the SSL options) are stored and
    reused for both the coordinator and the data-instance connections.

    Parameters:

       * :obj:`host` / :obj:`address` / :obj:`port`

            The seed coordinator to contact for the first routing-table fetch.

       * :obj:`resolver`

            Optional callable mapping an advertised ``"host:port"`` address to
            an iterable of ``"host:port"`` targets to try, in order.  Defaults
            to using the advertised address unchanged.

       * :obj:`routing_context`

            Optional :class:`dict` passed through to
            :meth:`Connection.get_routing_table`.
    """

    def __init__(
        self,
        *,
        host=None,
        address=None,
        port=None,
        resolver=None,
        routing_context=None,
        **connect_kwargs,
    ):
        # The seed coordinator address.  "address" (a numeric IP) and "host" are
        # interchangeable connect targets; store whichever was given.
        self._seed_host = address or host
        self._seed_port = port

        self._resolver = resolver if resolver is not None else _default_resolver
        self._routing_context = (
            routing_context if routing_context is not None else {}
        )

        # Parameters reused for the data-instance connection (honours "lazy")
        # and for the coordinator connection (ROUTE needs an idle, non-lazy
        # session, so "lazy" is dropped there).
        self._data_kwargs = dict(connect_kwargs)
        self._coordinator_kwargs = dict(connect_kwargs)
        self._coordinator_kwargs.pop("lazy", None)

        self._table = None
        self._expires_at = 0.0
        self._read_index = 0
        self._refresh_count = 0
        self._lock = threading.Lock()

    # -- routing table management -------------------------------------------

    def _router_targets_locked(self):
        """Coordinator ``(host, port)`` targets to try when refreshing.

        The seed comes first, followed by the ROUTE-role servers from the last
        known table (resolved through ``resolver``), so a refresh survives the
        seed coordinator going away.
        """
        targets = []
        if self._seed_host is not None:
            targets.append((self._seed_host, self._seed_port))
        if self._table is not None:
            for advertised in self._table.route:
                for target in self._resolver(advertised):
                    host_port = _split_host_port(target)
                    if host_port not in targets:
                        targets.append(host_port)
        return targets

    def _refresh_locked(self):
        """Fetch a fresh routing table; assumes ``self._lock`` is held."""
        errors = []
        for host, port in self._router_targets_locked():
            try:
                coordinator = _base_connect(
                    host=host, port=port, **self._coordinator_kwargs
                )
            except OperationalError as exc:
                errors.append(f"{host}:{port}: {exc}")
                continue
            try:
                raw = coordinator.get_routing_table(
                    routing_context=self._routing_context
                )
            finally:
                coordinator.close()
            self._table = RoutingTable.parse(raw)
            self._expires_at = time.monotonic() + max(self._table.ttl, 0)
            self._refresh_count += 1
            return

        raise OperationalError(
            "could not fetch routing table from any coordinator: "
            + "; ".join(errors)
        )

    def _ensure_fresh(self):
        with self._lock:
            if self._table is None or time.monotonic() >= self._expires_at:
                self._refresh_locked()

    def refresh(self):
        """Force an immediate refresh of the cached routing table."""
        with self._lock:
            self._refresh_locked()

    @property
    def routing_table(self):
        """A snapshot of the (refreshed if stale) routing table as a dict."""
        self._ensure_fresh()
        with self._lock:
            table = self._table
            return {
                "ttl": table.ttl,
                "write": list(table.write),
                "read": list(table.read),
                "route": list(table.route),
            }

    # -- server selection ----------------------------------------------------

    def _ordered_targets(self, access_mode):
        """Resolved ``"host:port"`` candidates for ``access_mode``, in order.

        Reads round-robin across replicas so successive READ connections start
        at different servers; the remaining servers stay as failover
        candidates.
        """
        with self._lock:
            if self._table is None:
                servers = []
            else:
                servers = self._table.servers(access_mode)
                if access_mode == ACCESS_MODE_READ and servers:
                    start = self._read_index % len(servers)
                    self._read_index += 1
                    servers = servers[start:] + servers[:start]
                else:
                    servers = list(servers)

        candidates = []
        for advertised in servers:
            for target in self._resolver(advertised):
                if target not in candidates:
                    candidates.append(target)
        return candidates

    def _try_connect(self, candidates):
        errors = []
        for target in candidates:
            host, port = _split_host_port(target)
            attempt = dict(self._data_kwargs)
            attempt["host"] = host
            attempt["port"] = port
            try:
                return _base_connect(**attempt), None
            except OperationalError as exc:
                errors.append(f"{target}: {exc}")
        return None, "; ".join(errors)

    def connect(self, access_mode=ACCESS_MODE_WRITE):
        """Open a connection to a data instance serving ``access_mode``.

        Returns an ordinary :class:`Connection`.  Raises
        :exc:`OperationalError` if no server for the requested access mode can
        be reached, even after refreshing the routing table.
        """
        access_mode = _normalize_access_mode(access_mode)

        last_error = None
        # Two attempts: the second runs against a freshly refreshed table in
        # case the topology changed (e.g. a failover) since it was cached.
        for attempt in range(2):
            self._ensure_fresh()
            candidates = self._ordered_targets(access_mode)
            if candidates:
                connection, error = self._try_connect(candidates)
                if connection is not None:
                    return connection
                last_error = error
            else:
                last_error = f"no {access_mode} server in the routing table"

            if attempt == 0:
                # Selected servers were unreachable (or none were listed);
                # discard the cached table and retry with a fresh one.
                self.refresh()

        raise OperationalError(
            f"could not connect to any {access_mode} server: {last_error}"
        )


def connect(
    *,
    routing=False,
    access_mode=ACCESS_MODE_WRITE,
    resolver=None,
    routing_context=None,
    **kwargs,
):
    """Make a new connection to a Memgraph server and return a
    :class:`Connection` object.

    Currently recognized connection parameters are:

       * :obj:`host`

            DNS resolvable name of host to connect to. Exactly one of host and
            address parameters must be specified.

       * :obj:`address`

            Numeric IP address of host to connect to. This should be in the
            standard IPv4 address format. You can also use IPv6 if your machine
            supports it. Exactly one of host and address parameters must be
            specified.

       * :obj:`port`

            Port number to connect to at the server host.

       * :obj:`username`

            Username to connect as.

       * :obj:`password`

            Password to be used if the server demands password authentication.

       * :obj:`client_name`

            Alternate name and version of the client to send to server. Default
            is set by the underlying mgclient library.

       * :obj:`sslmode`

            Whether a secure connection will be negotiated with the server;
            either :const:`mgclient.MG_SSLMODE_DISABLE` (default) or
            :const:`mgclient.MG_SSLMODE_REQUIRE`.

       * :obj:`sslcert`

            File name of the client SSL certificate. Ignored if an SSL
            connection is not made.

       * :obj:`sslkey`

            Location of the secret key used for the client certificate. Ignored
            if an SSL connection is not made.

       * :obj:`trust_callback`

            A callable taking four arguments (hostname, IP address, public key
            type and fingerprint), called after the SSL handshake. If it returns
            ``False`` the connection is terminated. Useful for implementing a
            TOFU (trust on first use) mechanism.

       * :obj:`lazy`

            If ``True``, a lazy connection is made. Default is ``False``.

    When ``routing`` is ``True`` the connection is made in a routing-aware way,
    suitable for a Memgraph high-availability cluster:

       * The ``host``/``port`` (or ``address``) parameters must point at a
         cluster coordinator.  A Bolt ``ROUTE`` message is sent to it to obtain
         the current cluster topology.

       * Depending on ``access_mode`` a server is selected from the routing
         table -- :data:`ACCESS_MODE_WRITE` (the default) selects the main and
         :data:`ACCESS_MODE_READ` selects a replica.

       * A normal connection is then opened to that server and returned.  If a
         selected server cannot be reached, the remaining candidates are tried
         in turn; an :exc:`OperationalError` is raised only if none succeed.

    This is a one-shot convenience: it performs a fresh ``ROUTE`` lookup on
    every call.  For a long-lived, caching, load-balancing router, use
    :class:`Router` directly.

    All other connection parameters (authentication, SSL, ``lazy``, ...) are
    applied to the returned data-instance connection.

    Extra routing parameters:

       * :obj:`routing`

            If ``True``, enable client-side routing as described above.
            Defaults to ``False``.

       * :obj:`access_mode`

            Either :data:`ACCESS_MODE_WRITE` or :data:`ACCESS_MODE_READ`.
            Defaults to :data:`ACCESS_MODE_WRITE`.

       * :obj:`resolver`

            Optional callable mapping an advertised ``"host:port"`` address
            from the routing table to an iterable of ``"host:port"`` targets to
            actually try (in order).  Defaults to using the advertised address
            unchanged.  This is the hook to use when the addresses the cluster
            advertises are not directly reachable by the client (for example
            behind a proxy or port-forward).

       * :obj:`routing_context`

            Optional :class:`dict` passed through to
            :meth:`Connection.get_routing_table` as the routing context.
    """
    if not routing:
        return _base_connect(**kwargs)

    router = Router(resolver=resolver, routing_context=routing_context, **kwargs)
    return router.connect(access_mode=access_mode)
