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

import os
import subprocess
import sys
import time
import tempfile
import pytest
import mgclient

MEMGRAPH_PATH = os.getenv("MEMGRAPH_PATH", '/usr/lib/memgraph/memgraph')
MEMGRAPH_PORT = int(os.getenv("MEMGRAPH_PORT", 7687))
MEMGRAPH_HOST = os.getenv("MEMGRAPH_HOST", None)
MEMGRAPH_STARTED_WITH_SSL = os.getenv('MEMGRAPH_STARTED_WITH_SSL', None)
DURABILITY_DIR = tempfile.TemporaryDirectory()


def wait_for_server(port):
    cmd = ["nc", "-z", "-w", "1", "127.0.0.1", str(port)]
    count = 0
    while subprocess.call(cmd) != 0:
        time.sleep(0.1)
        if count > 100:
            raise RuntimeError(
                "Could not wait for server on port",
                port,
                "to startup!")
            sys.exit(1)
        count += 1


requires_ssl_enabled = pytest.mark.skipif(
    MEMGRAPH_HOST is not None
    and MEMGRAPH_STARTED_WITH_SSL is None,
    reason="requires secure connection")

requires_ssl_disabled = pytest.mark.skipif(
    MEMGRAPH_HOST is not None
    and MEMGRAPH_STARTED_WITH_SSL is not None,
    reason="requires insecure connection")


class Memgraph:
    def __init__(self, host, port, use_ssl, process):
        self.host = host
        self.port = port
        self.use_ssl = use_ssl
        self.process = process

    def is_long_running(self):
        return self.process is None

    def sslmode(self):
        return mgclient.MG_SSLMODE_REQUIRE if self.use_ssl else mgclient.MG_SSLMODE_DISABLE

    def kill(self):
        if self.process:
            self.process.kill()


def start_memgraph(cert_file="", key_file=""):
    if MEMGRAPH_HOST:
        use_ssl = MEMGRAPH_STARTED_WITH_SSL is not None
        return Memgraph(MEMGRAPH_HOST, MEMGRAPH_PORT, use_ssl, None)

    cmd = [MEMGRAPH_PATH,
           "--bolt-port", str(MEMGRAPH_PORT),
           "--bolt-cert-file", cert_file,
           "--bolt-key-file", key_file,
           "--data-directory", DURABILITY_DIR.name,
           "--storage-properties-on-edges=true",
           "--storage-snapshot-interval-sec=0",
           "--storage-wal-enabled=false",
           "--storage-recover-on-startup=false",
           "--storage-snapshot-on-exit=false",
           "--telemetry-enabled=false",
           "--log-file", ""]
    memgraph_process = subprocess.Popen(cmd)
    wait_for_server(MEMGRAPH_PORT)
    use_ssl = True if key_file.strip() else False
    return Memgraph('localhost', MEMGRAPH_PORT, use_ssl, memgraph_process)
