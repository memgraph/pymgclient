# Copyright (c) 2016-2019 Memgraph Ltd. [https://memgraph.com]
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

MEMGRAPH_PATH = os.getenv("MEMGRAPH_PATH", '/usr/lib/memgraph/memgraph')
MEMGRAPH_PORT = int(os.getenv("MEMGRAPH_PORT", 7687))
DURABILITY_DIR = tempfile.TemporaryDirectory()


def wait_for_server(port):
    cmd = ["nc", "-z", "-w", "1", "127.0.0.1", str(port)]
    count = 0
    while subprocess.call(cmd) != 0:
        time.sleep(0.01)
        if count > 100:
            raise RuntimeError(
                "Could not wait for server on port",
                port,
                "to startup!")
            sys.exit(1)
        count += 1
    time.sleep(0.01)


def start_memgraph(cert_file="", key_file=""):
    cmd = [MEMGRAPH_PATH,
           "--port", str(MEMGRAPH_PORT),
           "--cert-file", cert_file,
           "--key-file", key_file,
           "--durability-directory", DURABILITY_DIR.name,
           "--db-recover-on-startup=false",
           "--durability-enabled=false",
           "--properties-on-disk", "",
           "--snapshot-on-exit=false",
           "--telemetry-enabled=false"]
    memgraph = subprocess.Popen(cmd)
    wait_for_server(MEMGRAPH_PORT)
    return memgraph
