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

"""Shared fixtures for the test suite.

The high-availability fixtures here are used by both ``test_connection.py``
(the ``get_routing_table`` primitive) and ``test_routing.py`` (the client-side
routing built on top of it).
"""

import os
import time

import mgclient
import pytest

from common import (
    MEMGRAPH_HA_COORDINATOR_HOST,
    MEMGRAPH_HA_COORDINATOR_PORT,
)

# Topology created by the "Run Memgraph HA Cluster" CI step. The fixture below
# must agree with the container names and ports used there.
HA_COORDINATORS = ["mg-coord1", "mg-coord2", "mg-coord3"]
HA_DATA_INSTANCES = [("instance_1", "mg-data1"), ("instance_2", "mg-data2")]
HA_MAIN = "instance_1"
HA_BOLT_PORT = 7687
HA_COORDINATOR_PORT = 12121
HA_MANAGEMENT_PORT = 13011
HA_REPLICATION_PORT = 10000


def _ha_admin(conn, query):
    """Run a coordinator admin query, tolerating idempotent re-runs."""
    cursor = conn.cursor()
    try:
        cursor.execute(query)
        try:
            cursor.fetchall()
        except mgclient.Error:
            pass
    except mgclient.DatabaseError as exc:
        # Re-running setup against an already-configured cluster is fine.
        print(f"HA setup query ignored error: {exc}")


@pytest.fixture(scope="module")
def ha_cluster():
    host = MEMGRAPH_HA_COORDINATOR_HOST
    port = MEMGRAPH_HA_COORDINATOR_PORT

    conn = mgclient.connect(host=host, port=port)
    conn.autocommit = True

    # mg-coord1 is the bootstrap coordinator; add the remaining ones.
    for cid, name in enumerate(HA_COORDINATORS, start=1):
        if cid == 1:
            continue
        _ha_admin(
            conn,
            f'ADD COORDINATOR {cid} WITH CONFIG '
            f'{{"bolt_server": "{name}:{HA_BOLT_PORT}", '
            f'"coordinator_server": "{name}:{HA_COORDINATOR_PORT}", '
            f'"management_server": "{name}:{HA_MANAGEMENT_PORT}"}}',
        )

    for name, data_host in HA_DATA_INSTANCES:
        _ha_admin(
            conn,
            f'REGISTER INSTANCE {name} WITH CONFIG '
            f'{{"bolt_server": "{data_host}:{HA_BOLT_PORT}", '
            f'"management_server": "{data_host}:{HA_MANAGEMENT_PORT}", '
            f'"replication_server": "{data_host}:{HA_REPLICATION_PORT}"}}',
        )

    _ha_admin(conn, f"SET INSTANCE {HA_MAIN} TO MAIN")

    # Wait for the cluster to converge and advertise all roles: the main
    # (WRITE), the replica (READ) and the coordinators (ROUTE).
    timeout = 60
    for _ in range(timeout):
        table = conn.get_routing_table()
        roles = {server["role"] for server in table["servers"]}
        if {"READ", "WRITE", "ROUTE"} <= roles:
            break
        time.sleep(1)
    else:
        conn.close()
        raise RuntimeError(f"HA cluster did not converge: {table}")

    yield host, port

    conn.close()


def _parse_address_map(raw):
    """Parse ``adv1=target1,adv2=target2`` into a ``dict``."""
    mapping = {}
    for pair in raw.split(","):
        pair = pair.strip()
        if not pair:
            continue
        advertised, _, target = pair.partition("=")
        mapping[advertised.strip()] = target.strip()
    return mapping


def resolve_ha_address(address):
    """Map a routing-table address to a locally reachable one.

    In CI the cluster runs on the same Docker network as the test runner, so
    the advertised addresses (e.g. ``mg-data1:7687``) resolve directly and this
    is the identity function.  When running against a cluster whose advertised
    addresses are not directly reachable (for example a Kubernetes cluster
    reached through ``kubectl port-forward``), set ``MEMGRAPH_HA_ADDRESS_MAP``
    to a comma-separated list of ``advertised=target`` pairs, e.g.::

        MEMGRAPH_HA_ADDRESS_MAP="memgraph-data-0.default.svc.cluster.local:7687=127.0.0.1:17690,..."
    """
    raw = os.getenv("MEMGRAPH_HA_ADDRESS_MAP")
    mapping = _parse_address_map(raw) if raw else {}
    return mapping.get(address, address)


@pytest.fixture(scope="module")
def ha_resolver():
    """A ``resolver`` callable suitable for the current environment."""

    def resolver(address):
        return [resolve_ha_address(address)]

    return resolver
