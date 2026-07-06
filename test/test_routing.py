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

"""Tests for client-side routing (``connect(routing=True, ...)``).

These run against a Memgraph high-availability cluster; they are skipped unless
``MEMGRAPH_HA_COORDINATOR_HOST`` is set.  See ``conftest.resolve_ha_address``
for how the advertised cluster addresses are mapped to reachable ones.
"""

import mgclient
import pytest

from common import requires_ha_cluster
from conftest import resolve_ha_address
import mgclient.routing as routing
from mgclient.routing import (
    Router,
    RoutingTable,
    is_committed_on_main_error,
    is_transient_error,
)


def _replication_role(conn):
    """Return 'main' or 'replica' for the instance a connection is bound to."""
    conn.autocommit = True
    cursor = conn.cursor()
    cursor.execute("SHOW REPLICATION ROLE")
    return cursor.fetchall()[0][0]


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

    with pytest.raises(mgclient.OperationalError):
        mgclient.connect(
            host=host, port=port, routing=True, access_mode="WRITE", resolver=resolver
        )


# ---------------------------------------------------------------------------
# Router: cached routing table, load balancing and failover (no cluster needed
# for the pure-logic tests below).
# ---------------------------------------------------------------------------


def test_routing_table_parse_groups_addresses_by_role():
    raw = {
        "ttl": 120,
        "servers": [
            {"role": "WRITE", "addresses": ["m:7687"]},
            {"role": "READ", "addresses": ["r1:7687", "r2:7687"]},
            {"role": "ROUTE", "addresses": ["c1:7687", "c2:7687"]},
            {"role": "SOMETHING_ELSE", "addresses": ["x:7687"]},
        ],
    }
    table = RoutingTable.parse(raw)

    assert table.write == ["m:7687"]
    assert table.read == ["r1:7687", "r2:7687"]
    assert table.route == ["c1:7687", "c2:7687"]
    assert table.ttl == 120


def _router_with_cached_table(table):
    # Router.__init__ opens no connection; we inject a table and a far-future
    # expiry to exercise selection logic without a cluster.
    router = Router(host="unused", port=7687)
    router._table = table
    router._expires_at = float("inf")
    return router


def test_read_targets_round_robin_across_replicas():
    table = RoutingTable(["m:7687"], ["r1:7687", "r2:7687", "r3:7687"], ["c:7687"], 300)
    router = _router_with_cached_table(table)

    firsts = [router._ordered_targets("READ")[0] for _ in range(6)]

    # Each READ selection starts at the next replica, cycling through them all.
    assert firsts == [
        "r1:7687",
        "r2:7687",
        "r3:7687",
        "r1:7687",
        "r2:7687",
        "r3:7687",
    ]
    # Every replica remains a failover candidate regardless of starting point.
    assert set(router._ordered_targets("READ")) == {"r1:7687", "r2:7687", "r3:7687"}


def test_write_targets_do_not_rotate():
    table = RoutingTable(["m:7687"], ["r1:7687", "r2:7687"], ["c:7687"], 300)
    router = _router_with_cached_table(table)

    assert router._ordered_targets("WRITE") == ["m:7687"]
    assert router._ordered_targets("WRITE") == ["m:7687"]


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
def test_router_caches_routing_table(ha_cluster, ha_resolver):
    host, port = ha_cluster
    router = Router(host=host, port=port, resolver=ha_resolver)

    router.connect(access_mode="WRITE").close()
    assert router._refresh_count == 1

    # Subsequent connects within the TTL are served from the cached table, with
    # no further ROUTE round-trips to a coordinator.
    router.connect(access_mode="READ").close()
    router.connect(access_mode="WRITE").close()
    assert router._refresh_count == 1


@requires_ha_cluster
def test_router_refresh_forces_new_lookup(ha_cluster, ha_resolver):
    host, port = ha_cluster
    router = Router(host=host, port=port, resolver=ha_resolver)

    router.connect(access_mode="WRITE").close()
    before = router._refresh_count

    router.refresh()
    assert router._refresh_count == before + 1


@requires_ha_cluster
def test_router_expired_table_is_refreshed(ha_cluster, ha_resolver):
    host, port = ha_cluster
    router = Router(host=host, port=port, resolver=ha_resolver)

    router.connect(access_mode="WRITE").close()
    before = router._refresh_count

    # Simulate the TTL having elapsed.
    router._expires_at = 0.0
    router.connect(access_mode="WRITE").close()
    assert router._refresh_count == before + 1


@requires_ha_cluster
def test_router_survives_seed_coordinator_failure(ha_cluster, ha_resolver):
    # After the first refresh the table lists every coordinator. If the seed
    # coordinator later becomes unreachable, a refresh must fall back to a
    # ROUTE-role server from the cached table.
    host, port = ha_cluster
    router = Router(host=host, port=port, resolver=ha_resolver)

    router.connect(access_mode="WRITE").close()

    # Break the seed; refresh should still succeed via a cached coordinator.
    router._seed_host, router._seed_port = "127.0.0.1", 1
    router.refresh()

    writer = router.connect(access_mode="WRITE")
    assert _replication_role(writer) == "main"
    writer.close()


@requires_ha_cluster
def test_router_refreshes_when_targets_unreachable(ha_cluster):
    host, port = ha_cluster

    # The coordinator (seed) is reachable, but the data instances are not, so
    # routing should refresh and retry before giving up.
    def resolver(address):
        return ["127.0.0.1:1"]

    router = Router(host=host, port=port, resolver=resolver)

    with pytest.raises(mgclient.OperationalError):
        router.connect(access_mode="WRITE")

    assert router._refresh_count >= 2


@requires_ha_cluster
def test_router_routing_table_property(ha_cluster, ha_resolver):
    host, port = ha_cluster
    router = Router(host=host, port=port, resolver=ha_resolver)

    table = router.routing_table
    assert table["write"]
    assert table["read"]
    assert table["route"]
    assert isinstance(table["ttl"], int)


# ---------------------------------------------------------------------------
# Managed retry / transaction functions (no cluster needed for these).
# ---------------------------------------------------------------------------

# Real failover messages observed against a chaos cluster.
_FAILOVER_MESSAGES = [
    "Write queries currently forbidden on the main instance. The cluster is in "
    "the process of setting up a new main instance, please retry the query "
    "later on.",
    "could not connect to any WRITE server: no WRITE server in the routing table",
    "memgraph-data-0:7687: couldn't connect to host: Connection refused",
    "failed to receive chunk size",
]

_COMMITTED_ON_MAIN_MESSAGE = (
    "Replication Exception: Failed to replicate to SYNC replica 'instance_1': "
    "replica is not reachable or not in sync with the main. Replica will be "
    "recovered automatically. Transaction is still committed on the main "
    "instance and other alive replicas."
)


@pytest.mark.parametrize("message", _FAILOVER_MESSAGES)
def test_is_transient_error_matches_failover_conditions(message):
    assert is_transient_error(mgclient.DatabaseError(message))


def test_is_transient_error_false_for_query_errors():
    assert not is_transient_error(mgclient.DatabaseError("Syntax error near 'FOO'"))


def test_committed_on_main_error_needs_both_markers():
    assert is_committed_on_main_error(mgclient.DatabaseError(_COMMITTED_ON_MAIN_MESSAGE))
    # A replication error without the durability note is not committed-on-main.
    assert not is_committed_on_main_error(
        mgclient.DatabaseError("Replication Exception: something unrelated")
    )
    assert not is_committed_on_main_error(mgclient.DatabaseError("Syntax error"))


class _FakeCursor:
    def execute(self, *args, **kwargs):
        pass

    def fetchall(self):
        return []


class _FakeConnection:
    """A stand-in connection so the retry loop can be tested without a server."""

    commit_error = None

    def __init__(self):
        self.autocommit = False

    def cursor(self):
        return _FakeCursor()

    def commit(self):
        if self.commit_error is not None:
            raise self.commit_error

    def close(self):
        pass


def _router_with_stubbed_connect(monkeypatch, connection_factory):
    router = Router(host="unused", port=7687)
    monkeypatch.setattr(
        router, "connect", lambda access_mode=routing.ACCESS_MODE_WRITE: connection_factory()
    )
    monkeypatch.setattr(router, "refresh", lambda: None)
    monkeypatch.setattr(routing.time, "sleep", lambda _seconds: None)
    return router


def test_execute_write_retries_transient_then_succeeds(monkeypatch):
    router = _router_with_stubbed_connect(monkeypatch, _FakeConnection)

    calls = {"n": 0}

    def work(cursor):
        calls["n"] += 1
        if calls["n"] < 3:
            raise mgclient.DatabaseError(
                "setting up a new main, please retry the query later on"
            )
        return "done"

    assert router.execute_write(work) == "done"
    assert calls["n"] == 3


def test_execute_write_treats_committed_on_main_as_success(monkeypatch):
    class CommitFails(_FakeConnection):
        commit_error = mgclient.DatabaseError(_COMMITTED_ON_MAIN_MESSAGE)

    router = _router_with_stubbed_connect(monkeypatch, CommitFails)

    calls = {"n": 0}

    def work(cursor):
        calls["n"] += 1
        return "created"

    # The write is durable on the main, so it counts as success and must NOT be
    # retried (retrying would duplicate the write).
    assert router.execute_write(work) == "created"
    assert calls["n"] == 1


def test_execute_write_raises_non_transient_immediately(monkeypatch):
    router = _router_with_stubbed_connect(monkeypatch, _FakeConnection)

    calls = {"n": 0}

    def work(cursor):
        calls["n"] += 1
        raise mgclient.DatabaseError("Syntax error near 'FOO'")

    with pytest.raises(mgclient.DatabaseError):
        router.execute_write(work)
    assert calls["n"] == 1


def test_execute_write_gives_up_after_max_retries(monkeypatch):
    router = _router_with_stubbed_connect(monkeypatch, _FakeConnection)

    calls = {"n": 0}

    def work(cursor):
        calls["n"] += 1
        raise mgclient.DatabaseError("please retry the query later on")

    with pytest.raises(mgclient.DatabaseError):
        router.execute_write(work)
    assert calls["n"] == router._max_retries


def test_execute_read_retries_transient_then_succeeds(monkeypatch):
    router = _router_with_stubbed_connect(monkeypatch, _FakeConnection)

    calls = {"n": 0}

    def work(cursor):
        calls["n"] += 1
        if calls["n"] < 2:
            raise mgclient.OperationalError(
                "could not connect to any READ server: no READ server in the routing table"
            )
        return 7

    assert router.execute_read(work) == 7
    assert calls["n"] == 2


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
