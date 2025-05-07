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

import mgclient
import pytest
import tempfile

from common import start_memgraph, MEMGRAPH_PORT


def assert_db(cursor, db_name):
    cursor.execute("SHOW DATABASE")
    assert cursor.fetchall() == [(db_name, )]

def assert_data(cursor, db):
    cursor.execute('MATCH (n:Node) RETURN n.db')
    cursor.fetchall() == [(db,)]


@pytest.fixture(scope="function")
def memgraph_server():
    # memgraph = start_memgraph()
    yield "127.0.0.1", MEMGRAPH_PORT

    # memgraph.kill()
    # memgraph.wait()

# def test_connect_database_fail(memgraph_server):
#     host, port = memgraph_server
#     # Connected to a non existent database
#     conn = mgclient.connect(
#             host=host,
#             port=port,
#             lazy=True,
#             database="does not exist")
#     cursor = conn.cursor()
#     with pytest.raises(mgclient.DatabaseError):
#         cursor.execute("MATCH(n) RETURN n;")

# def test_connect_database(memgraph_server):
#     host, port = memgraph_server
#     conn = mgclient.connect(host=host, port=port, lazy=True)
#     cursor = conn.cursor()
    
#     #setup
#     assert_db(cursor, "memgraph")
#     cursor.execute('CREATE (:Node{db:"memgraph"})')
#     cursor.fetchall()
    
#     cursor.execute("CREATE DATABASE db1")
#     cursor.fetchall()
#     cursor.execute("USE DATABASE db1")
#     cursor.fetchall()
#     assert_db(cursor, "db1")
#     cursor.execute('CREATE (:Node{db:"db1"})')
#     cursor.fetchall()

#     cursor.execute("CREATE DATABASE db2")
#     cursor.fetchall()
#     cursor.execute("USE DATABASE db2")
#     cursor.fetchall()
#     assert_db(cursor, "db2")
#     cursor.execute('CREATE (:Node{db:"db2"})')
#     cursor.fetchall()

#     #connection tests
#     #default
#     conn = mgclient.connect(host=host, port=port, lazy=True)
#     cursor = conn.cursor()
#     assert_db(cursor, "memgraph")
#     assert_data(cursor, "memgraph")

#     #memgraph
#     conn = mgclient.connect(host=host, port=port, lazy=True, database="memgraph")
#     cursor = conn.cursor()
#     assert_db(cursor, "memgraph")
#     assert_data(cursor, "memgraph")

#     #db1
#     conn = mgclient.connect(host=host, port=port, lazy=True, database="db1")
#     cursor = conn.cursor()
#     assert_db(cursor, "db1")
#     assert_data(cursor, "db1")

#     #db2
#     conn = mgclient.connect(host=host, port=port, lazy=True, database="db2")
#     cursor = conn.cursor()
#     assert_db(cursor, "db2")
#     assert_data(cursor, "db2")

def test_connect_database_and_block(memgraph_server):
    host, port = memgraph_server
    conn = mgclient.connect(host=host, port=port, lazy=True)
    cursor = conn.cursor()
    
    #setup
    assert_db(cursor, "memgraph")
    cursor.execute("CREATE DATABASE db1")
    cursor.fetchall()
    cursor.execute("CREATE DATABASE db2")
    cursor.fetchall()

    #connection tests
    #default <- should allow db switching
    conn = mgclient.connect(host=host, port=port, lazy=True)
    cursor = conn.cursor()
    assert_db(cursor, "memgraph")
    cursor.execute("USE DATABASE db1;")
    cursor.fetchall()
    assert_db(cursor, "db1")
    cursor.execute("USE DATABASE db2;")
    cursor.fetchall()
    assert_db(cursor, "db2")

    #memgraph
    conn = mgclient.connect(host=host, port=port, lazy=True, database="memgraph")
    cursor = conn.cursor()
    assert_db(cursor, "memgraph")
    with pytest.raises(mgclient.DatabaseError):
        cursor.execute("USE DATABASE db2;")
        print(cursor.fetchall())

    #db1
    conn = mgclient.connect(host=host, port=port, lazy=True, database="db1")
    cursor = conn.cursor()
    assert_db(cursor, "db1")
    with pytest.raises(mgclient.DatabaseError):
        cursor.execute("USE DATABASE db2;")
        cursor.fetchall()

    #db2
    conn = mgclient.connect(host=host, port=port, lazy=True, database="db2")
    cursor = conn.cursor()
    assert_db(cursor, "db2")
    with pytest.raises(mgclient.DatabaseError):
        cursor.execute("USE DATABASE memgraph;")
        cursor.fetchall()
