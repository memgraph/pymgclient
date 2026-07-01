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
