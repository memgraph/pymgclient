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
"""

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


def _routed_connect(access_mode, resolver, routing_context, kwargs):
    if isinstance(access_mode, str):
        access_mode = access_mode.upper()
    if access_mode not in _ACCESS_MODES:
        raise ValueError(
            f"access_mode must be one of {_ACCESS_MODES!r}, got {access_mode!r}"
        )
    if resolver is None:
        resolver = _default_resolver

    # Bootstrap: connect to the coordinator and fetch the routing table.  ROUTE
    # requires an idle, non-lazy session, so drop "lazy" for this connection
    # only (the final connection still honours the caller's choice).
    bootstrap_kwargs = dict(kwargs)
    bootstrap_kwargs.pop("lazy", None)

    coordinator = _base_connect(**bootstrap_kwargs)
    try:
        context = routing_context if routing_context is not None else {}
        table = coordinator.get_routing_table(routing_context=context)
    finally:
        coordinator.close()

    # Collect the advertised addresses of every server that serves the
    # requested access mode, preserving the coordinator's ordering.
    advertised = []
    for server in table.get("servers", []):
        if server.get("role") == access_mode:
            advertised.extend(server.get("addresses", []))

    if not advertised:
        raise OperationalError(
            f"no {access_mode} server available in the routing table"
        )

    # Map advertised addresses to reachable connect targets, de-duplicating
    # while keeping order so failover is deterministic.
    candidates = []
    for address in advertised:
        for target in resolver(address):
            if target not in candidates:
                candidates.append(target)

    # The final connection reuses the caller's parameters (auth, SSL, lazy, ...)
    # but is directed at the resolved data instance.  "address" (a numeric IP
    # for the coordinator) never applies to the routed target.
    final_kwargs = dict(kwargs)
    final_kwargs.pop("address", None)

    errors = []
    for target in candidates:
        host, port = _split_host_port(target)
        attempt = dict(final_kwargs)
        attempt["host"] = host
        attempt["port"] = port
        try:
            return _base_connect(**attempt)
        except OperationalError as exc:
            errors.append(f"{target}: {exc}")

    raise OperationalError(
        f"could not connect to any {access_mode} server; tried "
        + "; ".join(errors)
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

    return _routed_connect(
        access_mode=access_mode,
        resolver=resolver,
        routing_context=routing_context,
        kwargs=kwargs,
    )
