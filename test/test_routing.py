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

"""Tests for client-side routing (``connect(routing=True, ...)`` and
:class:`mgclient.Router`).

These exercise the Python facade over libmgclient's routing engine. The engine
itself -- routing-table parsing and caching, read round-robin, coordinator
failover and the managed-transaction retry loop -- is unit-tested in
libmgclient's own suite (``tests/routing.cpp``); the cluster-gated tests here
confirm the facade wires it up correctly end to end. They are skipped unless
``MEMGRAPH_HA_COORDINATOR_HOST`` is set. See ``conftest.resolve_ha_address``
for how the advertised cluster addresses are mapped to reachable ones.
"""

import mgclient
import pytest

from common import requires_ha_cluster
from conftest import resolve_ha_address
from mgclient.routing import (
    Router,
    is_transient_error,
)


def _replication_role(conn):
    """Return 'main' or 'replica' for the instance a connection is bound to."""
    conn.autocommit = True
    cursor = conn.cursor()
    cursor.execute("SHOW REPLICATION ROLE")
    return cursor.fetchall()[0][0]


# ---------------------------------------------------------------------------
# Argument handling (no cluster needed).
# ---------------------------------------------------------------------------


def test_access_mode_constants():
    # The access-mode constants match the routing-table role names so they read
    # naturally and can be used interchangeably with the plain strings.
    assert mgclient.ACCESS_MODE_WRITE == "WRITE"
    assert mgclient.ACCESS_MODE_READ == "READ"


def test_connect_routing_invalid_access_mode():
    # Validation happens before any connection is attempted, so this needs no
    # server.
    with pytest.raises(ValueError):
        mgclient.connect(
            host="127.0.0.1", port=7687, routing=True, access_mode="SIDEWAYS"
        )


def test_connect_rejects_positional_arguments():
    # connect() takes only keyword arguments, exactly like the underlying C
    # connect, so a positional host is a TypeError rather than being silently
    # treated as the routing flag.
    with pytest.raises(TypeError):
        mgclient.connect("127.0.0.1", 7687)


# ---------------------------------------------------------------------------
# connect(routing=True): one-shot routed connections (cluster-gated).
# ---------------------------------------------------------------------------


@requires_ha_cluster
def test_connect_routing_write_reaches_main(ha_cluster, ha_resolver):
    host, port = ha_cluster
    conn = mgclient.connect(
        host=host, port=port, routing=True, access_mode="WRITE", resolver=ha_resolver
    )
    assert conn.status == mgclient.CONN_STATUS_READY
    assert _replication_role(conn) == "main"
    conn.close()


@requires_ha_cluster
def test_connect_routing_read_reaches_replica(ha_cluster, ha_resolver):
    host, port = ha_cluster
    conn = mgclient.connect(
        host=host, port=port, routing=True, access_mode="READ", resolver=ha_resolver
    )
    assert conn.status == mgclient.CONN_STATUS_READY
    assert _replication_role(conn) == "replica"
    conn.close()


@requires_ha_cluster
def test_connect_routing_default_access_mode_is_write(ha_cluster, ha_resolver):
    host, port = ha_cluster
    conn = mgclient.connect(host=host, port=port, routing=True, resolver=ha_resolver)
    assert _replication_role(conn) == "main"
    conn.close()


@requires_ha_cluster
def test_connect_routing_access_mode_is_case_insensitive(ha_cluster, ha_resolver):
    host, port = ha_cluster
    conn = mgclient.connect(
        host=host, port=port, routing=True, access_mode="read", resolver=ha_resolver
    )
    assert _replication_role(conn) == "replica"
    conn.close()


@requires_ha_cluster
def test_connect_routing_resolver_receives_advertised_addresses(ha_cluster):
    host, port = ha_cluster

    seen = []

    def resolver(address):
        seen.append(address)
        return [resolve_ha_address(address)]

    conn = mgclient.connect(
        host=host, port=port, routing=True, access_mode="WRITE", resolver=resolver
    )
    # The resolver is consulted with the advertised "host:port" addresses from
    # the routing table.
    assert seen
    assert all(":" in address for address in seen)
    assert _replication_role(conn) == "main"
    conn.close()


@requires_ha_cluster
def test_connect_routing_fails_over_across_candidates(ha_cluster):
    host, port = ha_cluster

    # The first candidate is unreachable; routing must fall back to the next.
    def resolver(address):
        return ["127.0.0.1:1", resolve_ha_address(address)]

    conn = mgclient.connect(
        host=host, port=port, routing=True, access_mode="WRITE", resolver=resolver
    )
    assert _replication_role(conn) == "main"
    conn.close()


@requires_ha_cluster
def test_connect_routing_all_candidates_unreachable(ha_cluster):
    host, port = ha_cluster

    def resolver(address):
        return ["127.0.0.1:1"]

    # Being unable to reach any server is a transient cluster condition, so the
    # Router raises TransientError (a subclass of OperationalError).
    with pytest.raises(mgclient.TransientError):
        mgclient.connect(
            host=host, port=port, routing=True, access_mode="WRITE", resolver=resolver
        )


# ---------------------------------------------------------------------------
# Router: a reusable engine (cluster-gated).
# ---------------------------------------------------------------------------


@requires_ha_cluster
def test_router_connect_reaches_correct_instances(ha_cluster, ha_resolver):
    host, port = ha_cluster
    router = Router(host=host, port=port, resolver=ha_resolver)

    writer = router.connect(access_mode="WRITE")
    assert _replication_role(writer) == "main"
    writer.close()

    reader = router.connect(access_mode="READ")
    assert _replication_role(reader) == "replica"
    reader.close()


@requires_ha_cluster
def test_router_refreshes_when_targets_unreachable(ha_cluster):
    host, port = ha_cluster

    # The coordinator (seed) is reachable, but the data instances are not, so
    # routing refreshes and retries before giving up with a transient error.
    def resolver(address):
        return ["127.0.0.1:1"]

    router = Router(host=host, port=port, resolver=resolver)

    with pytest.raises(mgclient.TransientError):
        router.connect(access_mode="WRITE")


@requires_ha_cluster
def test_router_routing_table_property(ha_cluster, ha_resolver):
    host, port = ha_cluster
    router = Router(host=host, port=port, resolver=ha_resolver)

    table = router.routing_table
    assert table["write"]
    assert table["read"]
    assert table["route"]
    assert isinstance(table["ttl"], int)


@requires_ha_cluster
def test_router_refresh_succeeds(ha_cluster, ha_resolver):
    host, port = ha_cluster
    router = Router(host=host, port=port, resolver=ha_resolver)

    # An explicit refresh works before and after any connection is opened.
    router.refresh()
    router.connect(access_mode="WRITE").close()
    router.refresh()


# ---------------------------------------------------------------------------
# Managed transactions (cluster-gated).
# ---------------------------------------------------------------------------


@requires_ha_cluster
def test_execute_write_and_read_roundtrip(ha_cluster, ha_resolver):
    host, port = ha_cluster
    router = Router(host=host, port=port, resolver=ha_resolver)

    def create(cursor):
        cursor.execute(
            "CREATE (n:MgExecTest {tag: $tag}) RETURN n.tag", {"tag": "exec"}
        )
        return cursor.fetchall()[0][0]

    assert router.execute_write(create) == "exec"

    def count(cursor):
        cursor.execute("MATCH (n:MgExecTest {tag: 'exec'}) RETURN count(n)")
        return cursor.fetchall()[0][0]

    assert router.execute_read(count) >= 1

    router.execute_write(
        lambda cursor: cursor.execute("MATCH (n:MgExecTest) DETACH DELETE n")
    )


# ---------------------------------------------------------------------------
# Error-classification helper (no cluster needed).
# ---------------------------------------------------------------------------


def test_is_transient_error_recognizes_transient_error_type():
    assert is_transient_error(
        mgclient.TransientError("a server transient error, any message")
    )


def test_is_transient_error_false_for_non_transient_types():
    # Matches TransientError only -- not its parent types.
    assert not is_transient_error(mgclient.DatabaseError("Syntax error near 'FOO'"))
    assert not is_transient_error(mgclient.OperationalError("unexpected disconnect"))
