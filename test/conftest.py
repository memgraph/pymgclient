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

"""Shared fixtures for the test suite.

The high-availability fixtures here are used by both ``test_connection.py``
(the ``get_routing_table`` primitive) and ``test_routing.py`` (the client-side
routing built on top of it).
"""

import mgclient
import pytest

from common import (
    MEMGRAPH_HA_COORDINATOR_HOST,
    MEMGRAPH_HA_COORDINATOR_PORT,
)


@pytest.fixture(scope="module")
def ha_cluster():
    """The coordinator ``(host, port)`` of a pre-provisioned HA cluster.

    The cluster is created, registered and converged out of band -- in CI by
    the "Run Memgraph HA Cluster" step (``mgclient/tool/ha_cluster.sh``), or
    locally by running that script -- so this fixture only checks it is up and
    advertising all roles, then hands the coordinator address to the tests.
    """
    host = MEMGRAPH_HA_COORDINATOR_HOST
    port = MEMGRAPH_HA_COORDINATOR_PORT

    conn = mgclient.connect(host=host, port=port)
    try:
        roles = {
            server["role"] for server in conn.get_routing_table()["servers"]
        }
        if not {"READ", "WRITE", "ROUTE"} <= roles:
            raise RuntimeError(
                f"HA cluster not ready; routing table advertised roles: {roles}"
            )
    finally:
        conn.close()

    return host, port
