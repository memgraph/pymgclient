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

from common import start_memgraph, MEMGRAPH_PORT


@pytest.fixture(scope="function")
def memgraph_server():
    memgraph = start_memgraph()
    yield "127.0.0.1", MEMGRAPH_PORT

    memgraph.kill()


def test_cursor_visibility(memgraph_server):
    host, port = memgraph_server
    conn = mgclient.connect(host=host, port=port)

    cursor1 = conn.cursor()
    cursor1.execute("CREATE (:Node)")

    cursor2 = conn.cursor()
    cursor2.execute("MATCH (n) RETURN count(n)")
    assert cursor2.fetchall() == [(1, )]


class TestCursorInRegularConnection:
    def test_execute_closed_connection(self, memgraph_server):
        host, port = memgraph_server
        conn = mgclient.connect(host=host, port=port)

        cursor = conn.cursor()
        conn.close()

        with pytest.raises(mgclient.InterfaceError):
            cursor.execute("RETURN 100")

    def test_cursor_close(self, memgraph_server):
        host, port = memgraph_server
        conn = mgclient.connect(host=host, port=port)

        cursor = conn.cursor()
        cursor.execute("UNWIND range(1, 10) AS n RETURN n")

        cursor.close()

        # closing again does nothing
        cursor.close()

        with pytest.raises(mgclient.InterfaceError):
            cursor.fetchone()

        with pytest.raises(mgclient.InterfaceError):
            cursor.execute("RETURN 100")

        with pytest.raises(mgclient.InterfaceError):
            cursor.fetchmany()

        with pytest.raises(mgclient.InterfaceError):
            cursor.fetchall()

        with pytest.raises(mgclient.InterfaceError):
            cursor.setinputsizes([])

        with pytest.raises(mgclient.InterfaceError):
            cursor.setoutputsizes(100)

    def test_cursor_fetchone(self, memgraph_server):
        host, port = memgraph_server
        conn = mgclient.connect(host=host, port=port)

        cursor = conn.cursor()

        with pytest.raises(mgclient.InterfaceError):
            cursor.fetchone()

        cursor.execute("UNWIND range(1, 10) AS n RETURN n")

        for n in range(1, 11):
            assert cursor.fetchone() == (n, )

        assert cursor.fetchone() is None
        assert cursor.fetchone() is None

        cursor.execute("RETURN 100")
        assert cursor.fetchone() == (100, )
        assert cursor.fetchone() is None

    def test_cursor_fetchmany(self, memgraph_server):
        host, port = memgraph_server
        conn = mgclient.connect(host=host, port=port)

        cursor = conn.cursor()

        with pytest.raises(mgclient.InterfaceError):
            cursor.fetchmany()

        cursor.execute("UNWIND range(1, 10) AS n RETURN n")

        with pytest.raises(OverflowError):
            cursor.fetchmany(10**100)

        assert cursor.fetchmany() == [(1, )]

        cursor.arraysize = 4

        assert cursor.fetchmany() == [(2, ), (3, ), (4, ), (5, )]
        assert cursor.fetchmany() == [(6, ), (7, ), (8, ), (9, )]
        assert cursor.fetchmany() == [(10, )]
        assert cursor.fetchmany() == []
        assert cursor.fetchone() is None

        cursor.execute("RETURN 100")
        assert cursor.fetchmany() == [(100, )]
        assert cursor.fetchmany() == []
        assert cursor.fetchone() is None

    def test_cursor_fetchall(self, memgraph_server):
        host, port = memgraph_server
        conn = mgclient.connect(host=host, port=port)

        cursor = conn.cursor()

        with pytest.raises(mgclient.InterfaceError):
            cursor.fetchall()

        cursor.execute("UNWIND range(1, 10) AS n RETURN n")

        assert cursor.fetchall() == [(n, ) for n in range(1, 11)]
        assert cursor.fetchall() == []
        assert cursor.fetchone() is None

        cursor.execute("RETURN 100")

        assert cursor.fetchall() == [(100, )]
        assert cursor.fetchall() == []
        assert cursor.fetchone() is None

    def test_cursor_multiple_queries(self, memgraph_server):
        host, port = memgraph_server
        conn = mgclient.connect(host=host, port=port)

        cursor1 = conn.cursor()
        cursor2 = conn.cursor()

        cursor1.execute("UNWIND range(1, 10) AS n RETURN n")
        cursor2.execute("UNWIND range(1, 10) AS n RETURN n")

        for n in range(1, 11):
            assert cursor1.fetchone() == (n, )
            assert cursor2.fetchone() == (n, )

    def test_cursor_syntax_error(self, memgraph_server):
        host, port = memgraph_server
        conn = mgclient.connect(host=host, port=port)
        cursor = conn.cursor()

        cursor.execute("RETURN 100")

        with pytest.raises(mgclient.DatabaseError):
            cursor.execute("fjdkalfjdsalfaj")

        with pytest.raises(mgclient.InterfaceError):
            cursor.fetchall()

    def test_cursor_runtime_error(self, memgraph_server):
        host, port = memgraph_server
        conn = mgclient.connect(host=host, port=port)
        cursor = conn.cursor()

        cursor.execute("RETURN 100")

        with pytest.raises(mgclient.DatabaseError):
            cursor.execute("UNWIND [true, true, false] AS p RETURN assert(p)")
            cursor.fetchall()

        cursor.execute("RETURN 200")

        assert cursor.fetchall() == [(200, )]

    def test_cursor_description(self, memgraph_server):
        host, port = memgraph_server
        conn = mgclient.connect(host=host, port=port)
        cursor = conn.cursor()

        cursor.execute("RETURN 5 AS x, 6 AS y")
        assert len(cursor.description) == 2
        assert cursor.description[0].name == 'x'
        assert cursor.description[1].name == 'y'

        with pytest.raises(mgclient.DatabaseError):
            cursor.execute("jdfklfjkdalfja")

        assert cursor.description is None


class TestCursorInAsyncConnection:
    def test_cursor_close(self, memgraph_server):
        host, port = memgraph_server
        conn = mgclient.connect(host=host, port=port, lazy=True)

        cursor = conn.cursor()
        cursor.execute("UNWIND range(1, 10) AS n RETURN n")

        cursor2 = conn.cursor()

        with pytest.raises(mgclient.InterfaceError):
            cursor.close()

        cursor2.close()

        # NOTE: This here is a bit strange again because of double fetch /
        # server ahead of time pull because of the need for has_more info. As
        # soon as the last record is returned, the cursor will become
        # closeable.
        assert cursor.fetchmany(9) == [(n, ) for n in range(1, 10)]
        with pytest.raises(mgclient.InterfaceError):
            cursor.close()
        assert cursor.fetchone() == (10,)
        assert cursor.fetchone() is None

        cursor.close()

        # closing again does nothing
        cursor.close()

        with pytest.raises(mgclient.InterfaceError):
            cursor.fetchone()

        with pytest.raises(mgclient.InterfaceError):
            cursor.execute("RETURN 100")

        with pytest.raises(mgclient.InterfaceError):
            cursor.fetchmany()

        with pytest.raises(mgclient.InterfaceError):
            cursor.fetchall()

        with pytest.raises(mgclient.InterfaceError):
            cursor.setinputsizes([])

        with pytest.raises(mgclient.InterfaceError):
            cursor.setoutputsizes(100)

    def test_cursor_multiple_queries(self, memgraph_server):
        host, port = memgraph_server
        conn = mgclient.connect(host=host, port=port, lazy=True)

        cursor1 = conn.cursor()
        cursor2 = conn.cursor()

        cursor1.execute("UNWIND range(1, 10) AS n RETURN n")

        with pytest.raises(mgclient.InterfaceError):
            cursor2.execute("UNWIND range(1, 10) AS n RETURN n")

        assert cursor1.fetchall() == [(n, ) for n in range(1, 11)]

        with pytest.raises(mgclient.InterfaceError):
            cursor2.fetchall()

    def test_cursor_fetchone(self, memgraph_server):
        host, port = memgraph_server
        conn = mgclient.connect(host=host, port=port, lazy=True)

        cursor = conn.cursor()

        with pytest.raises(mgclient.InterfaceError):
            cursor.fetchone()

        cursor.execute("UNWIND range(1, 10) AS n RETURN n")

        for n in range(1, 11):
            assert cursor.fetchone() == (n, )

        assert cursor.fetchone() is None
        assert cursor.fetchone() is None

        cursor.execute("RETURN 100")
        assert cursor.fetchone() == (100, )
        assert cursor.fetchone() is None

    def test_cursor_fetchmany(self, memgraph_server):
        host, port = memgraph_server
        conn = mgclient.connect(host=host, port=port, lazy=True)

        cursor = conn.cursor()

        with pytest.raises(mgclient.InterfaceError):
            cursor.fetchmany()

        cursor.execute("UNWIND range(1, 10) AS n RETURN n")

        with pytest.raises(OverflowError):
            cursor.fetchmany(10**100)

        assert cursor.fetchmany() == [(1, )]

        cursor.arraysize = 4

        assert cursor.fetchmany() == [(2, ), (3, ), (4, ), (5, )]
        assert cursor.fetchmany() == [(6, ), (7, ), (8, ), (9, )]
        assert cursor.fetchmany() == [(10, )]
        assert cursor.fetchmany() == []
        assert cursor.fetchone() is None

        cursor.execute("RETURN 100")
        assert cursor.fetchmany() == [(100, )]
        assert cursor.fetchmany() == []
        assert cursor.fetchone() is None

    def test_cursor_fetchall(self, memgraph_server):
        host, port = memgraph_server
        conn = mgclient.connect(host=host, port=port, lazy=True)

        cursor = conn.cursor()

        with pytest.raises(mgclient.InterfaceError):
            cursor.fetchall()

        cursor.execute("UNWIND range(1, 10) AS n RETURN n")

        assert cursor.fetchall() == [(n, ) for n in range(1, 11)]
        assert cursor.fetchall() == []
        assert cursor.fetchone() is None

        cursor.execute("RETURN 100")

        assert cursor.fetchall() == [(100, )]
        assert cursor.fetchall() == []
        assert cursor.fetchone() is None

    def test_cursor_syntax_error(self, memgraph_server):
        host, port = memgraph_server
        conn = mgclient.connect(host=host, port=port, lazy=True)
        cursor = conn.cursor()

        cursor.execute("RETURN 100")
        cursor.fetchall()

        with pytest.raises(mgclient.DatabaseError):
            cursor.execute("fjdkalfjdsalfaj")

        with pytest.raises(mgclient.InterfaceError):
            cursor.fetchall()

    def test_cursor_runtime_error(self, memgraph_server):
        host, port = memgraph_server
        conn = mgclient.connect(host=host, port=port, lazy=True)
        cursor = conn.cursor()

        cursor.execute("RETURN 100")
        assert cursor.fetchall() == [(100, )]

        cursor.execute("UNWIND [true, true, false] AS p RETURN assert(p)")
        with pytest.raises(mgclient.DatabaseError):
            assert cursor.fetchone() == (True, )
            # NOTE: The exception is going to happen here which is unexpected.
            # The reason for that is because server pulls one more result ahead
            # of time to know are there more results.
            assert cursor.fetchone() == (True, )  # <- HERE
            cursor.fetchone()

        cursor.execute("UNWIND [true, true, false] AS p RETURN assert(p)")

        with pytest.raises(mgclient.DatabaseError):
            cursor.fetchmany(5)

        cursor.execute("UNWIND [true, true, false] AS p RETURN assert(p)")

        with pytest.raises(mgclient.DatabaseError):
            cursor.fetchall()

    def test_cursor_description(self, memgraph_server):
        host, port = memgraph_server
        conn = mgclient.connect(host=host, port=port, lazy=True)
        cursor = conn.cursor()

        cursor.execute("RETURN 5 AS x, 6 AS y")
        assert len(cursor.description) == 2
        assert cursor.description[0].name == 'x'
        assert cursor.description[1].name == 'y'

        cursor.fetchone()
        assert len(cursor.description) == 2
        assert cursor.description[0].name == 'x'
        assert cursor.description[1].name == 'y'

        cursor.fetchone()

        with pytest.raises(mgclient.DatabaseError):
            cursor.execute("jdfklfjkdalfja")

        assert cursor.description is None
