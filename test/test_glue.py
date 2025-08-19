# -*- coding: utf-8 -*-

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


import datetime
import platform
import sys

import mgclient
import pytest
from zoneinfo import ZoneInfo

from common import Memgraph, start_memgraph

# TODO(colinbarry) The Fedora docker image seems to have flaky timezone support.
# For the time being, we will force Fedora-based tests to use UTC only, and
# for other OSs to test more diverse timezones.
def is_fedora():
    """Check if running on Fedora platform"""
    try:
        with open('/etc/os-release', 'r') as f:
            content = f.read()
            return 'ID=fedora' in content
    except (FileNotFoundError, IOError):
        return False


@pytest.fixture(scope="function")
def memgraph_connection():
    memgraph = start_memgraph()
    conn = mgclient.connect(host=memgraph.host, port=memgraph.port, sslmode=memgraph.sslmode())
    conn.autocommit = True
    yield conn

    memgraph.terminate()
    conn.close()


def test_none(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()
    cursor.execute("RETURN $value", {"value": None})
    assert cursor.fetchall() == [(None,)]


def test_bool(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()

    cursor.execute("RETURN $value", {"value": True})
    assert cursor.fetchall() == [(True,)]

    cursor.execute("RETURN $value", {"value": False})
    assert cursor.fetchall() == [(False,)]


def test_integer(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()

    cursor.execute("RETURN $value", {"value": 42})
    assert cursor.fetchall() == [(42,)]

    cursor.execute("RETURN $value", {"value": -1})
    assert cursor.fetchall() == [(-1,)]

    cursor.execute("RETURN $value", {"value": 3289302198})
    assert cursor.fetchall() == [(3289302198,)]

    with pytest.raises(OverflowError):
        cursor.execute("RETURN $value", {"value": 10 ** 100})


def test_float(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()
    cursor.execute("RETURN $value", {"value": 42.0})
    assert cursor.fetchall() == [(42.0,)]

    cursor.execute("RETURN $value", {"value": 3.1415962})
    assert cursor.fetchall() == [(3.1415962,)]


def test_string(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()

    cursor.execute("RETURN $value", {"value": "the best test"})
    assert cursor.fetchall() == [("the best test",)]

    cursor.execute("RETURN '\u010C\u017D\u0160'")
    assert cursor.fetchall() == [("ČŽŠ",)]

    cursor.execute("RETURN $value", {"value": b"\x01\x0C\x01\x7D\x01\x60".decode("utf-16be")})
    assert cursor.fetchall() == [("ČŽŠ",)]


def test_list(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()

    cursor.execute("RETURN $value", {"value": [1, 2, None, True, False, "abc", []]})
    result = cursor.fetchall()
    assert result == [([1, 2, None, True, False, "abc", []],)]
    value_from_result = result[0][0][6]
    # This checks the reference number of the values in a mg_list are correct.
    # Ref count should be 3, because:
    #  * one reference because the list is referenced in result
    #  * one reference because of value_from_result
    #  * one temporary reference because sys.getrefcount increases the ref
    #    count
    assert sys.getrefcount(value_from_result) == 3


def test_map(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()
    key_in_a_map = """
    A long name because refs to strings are globally counted
    """
    value_in_a_map = [1, 2, 3]

    cursor.execute(
        "RETURN $value",
        {
            "value": {
                "x": 1,
                "y": 2,
                "map": {key_in_a_map: "value"},
                "list": value_in_a_map,
            }
        },
    )

    result = cursor.fetchall()
    assert result == [({"x": 1, "y": 2, "map": {key_in_a_map: "value"}, "list": value_in_a_map},)]

    value_in_a_map_from_result = result[0][0]["list"]
    # This checks if the reference number of the values in a mg_map are
    # correct.
    # Ref count should be 3, because:
    #  * one reference because the list is referenced in result
    #  * one reference because of value_in_a_map_from_result
    #  * one temporary reference because sys.getrefcount increases the ref
    #    count
    assert sys.getrefcount(value_in_a_map_from_result) == 3
    # This checks if the reference number of the keys in a mg_map are correct.
    # Ref count should be 3, because:
    #  * one reference because the string is referenced in result
    #  * one reference because of key_in_the_map (refs to the same strings are
    #    globally counted)
    #  * one temporary reference because sys.getrefcount increases the ref
    #    count
    assert sys.getrefcount(key_in_a_map) == 3


def test_node(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()
    cursor.execute("CREATE (n:Label1:Label2 {prop1 : 1, prop2 : 'prop2'}) " "RETURN id(n), n")
    rows = cursor.fetchall()
    node_id, _ = rows[0]
    assert rows == [
        (
            node_id,
            mgclient.Node(node_id, set(["Label1", "Label2"]), {"prop1": 1, "prop2": "prop2"}),
        )
    ]

    with pytest.raises(ValueError):
        cursor.execute("RETURN $node", {"node": rows[0][1]})


def test_relationship(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()
    cursor.execute("CREATE (n)-[e:Type {prop1 : 1, prop2 : 'prop2'}]->(m) " "RETURN id(n), id(m), id(e), e")
    rows = cursor.fetchall()
    start_id, end_id, edge_id, _ = rows[0]
    assert rows == [
        (
            start_id,
            end_id,
            edge_id,
            mgclient.Relationship(edge_id, start_id, end_id, "Type", {"prop1": 1, "prop2": "prop2"}),
        )
    ]

    with pytest.raises(ValueError):
        cursor.execute("RETURN $rel", {"rel": rows[0][3]})


def test_path(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()
    cursor.execute(
        "CREATE p = (n1:Node1)<-[e1:Edge1]-(n2:Node2)-[e2:Edge2]->(n3:Node3) "
        "RETURN id(n1), id(n2), id(n3), id(e1), id(e2), p"
    )
    rows = cursor.fetchall()
    n1_id, n2_id, n3_id, e1_id, e2_id, _ = rows[0]
    n1 = mgclient.Node(n1_id, set(["Node1"]), {})
    n2 = mgclient.Node(n2_id, set(["Node2"]), {})
    n3 = mgclient.Node(n3_id, set(["Node3"]), {})
    e1 = mgclient.Relationship(e1_id, n2_id, n1_id, "Edge1", {})
    e2 = mgclient.Relationship(e2_id, n2_id, n3_id, "Edge2", {})

    assert rows == [(n1_id, n2_id, n3_id, e1_id, e2_id, mgclient.Path([n1, n2, n3], [e1, e2]))]

    with pytest.raises(ValueError):
        cursor.execute("RETURN $path", {"path": rows[0][5]})


def test_tuple(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()

    cursor.execute("RETURN $value1, $value2", {"value1": [], "value2": []})
    result = cursor.fetchall()
    assert result == [([], [])]
    for i in [0, 1]:
        value_in_tuple = result[0][i]
        # This checks the reference number of the values in a tuple created
        # from an mg_list are correct.
        # Ref count should be 3, because:
        #  * one reference because the list is referenced in result
        #  * one reference because of value_in_tuple
        #  * one temporary reference because sys.getrefcount increases the ref
        #    count
        assert sys.getrefcount(value_in_tuple) == 3


@pytest.mark.temporal
def test_time(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()
    cursor.execute("RETURN $value", {"value": datetime.time(1, 2, 3, 40)})
    result = cursor.fetchall()
    assert result == [(datetime.time(1, 2, 3, 40),)]


@pytest.mark.temporal
def test_date(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()
    cursor.execute("RETURN $value", {"value": datetime.date(1994, 7, 12)})
    result = cursor.fetchall()
    assert result == [(datetime.date(1994, 7, 12),)]


@pytest.mark.temporal
def test_datetime(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()
    cursor.execute("RETURN $value", {"value": datetime.datetime(2004, 7, 11, 12, 13, 14, 15)})
    result = cursor.fetchall()
    assert result == [(datetime.datetime(2004, 7, 11, 12, 13, 14, 15),)]


@pytest.mark.temporal
def test_datetime_with_offset_timezone(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()
    cursor.execute("RETURN $value", {"value": datetime.datetime(2004, 7, 11, 12, 13, 14, 15, tzinfo=datetime.timezone(datetime.timedelta(hours=3)))})
    result = cursor.fetchall()
    assert result == [(datetime.datetime(2004, 7, 11, 12, 13, 14, 15, tzinfo=datetime.timezone(datetime.timedelta(hours=3))),)]

@pytest.mark.temporal
def test_datetime_with_named_timezone(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()

    # See comment at top of file about Fedora's timezone support.
    if is_fedora():
        tz_name = "UTC"
        expected_tz_names = ["UTC", "Etc/UTC"]
    else:
        tz_name = "Pacific/Pitcairn"
        expected_tz_names = ["Pacific/Pitcairn"]

    test_dt = datetime.datetime(2024, 8, 12, 10, 15, 42, 123, tzinfo=ZoneInfo(tz_name))
    cursor.execute("RETURN $value", {"value": test_dt})
    result = cursor.fetchall()
    assert len(result) == 1
    dt = result[0][0]
    assert isinstance(dt, datetime.datetime)
    assert dt.year == 2024
    assert dt.month == 8
    assert dt.day == 12
    assert dt.hour == 10
    assert dt.minute == 15
    assert dt.second == 42
    assert dt.microsecond == 123
    assert str(dt.tzinfo) in expected_tz_names


@pytest.mark.temporal
def test_duration(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()
    cursor.execute("RETURN $value", {"value": datetime.timedelta(64, 7, 11, 1)})
    result = cursor.fetchall()
    assert result == [(datetime.timedelta(64, 7, 1011),)]

@pytest.mark.temporal
def test_zoneddatetime(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()
    cursor.execute("RETURN $value", {"value": datetime.datetime(2025, 8, 12, 10, 15, 42, 123, tzinfo=datetime.timezone(datetime.timedelta(hours=7, minutes=30)))})
    result = cursor.fetchall()
    assert result == [(datetime.datetime(2025, 8, 12, 10, 15, 42, 123, tzinfo=datetime.timezone(datetime.timedelta(hours=7, minutes=30))),)]


@pytest.mark.temporal
def test_zoneddatetime_with_iana_timezone(memgraph_connection):
    conn = memgraph_connection
    cursor = conn.cursor()

    # See comment at top of file about Fedora's timezone support.
    if is_fedora():
        tz_name = "UTC"
        expected_tz_names = ["UTC", "Etc/UTC"]
    else:
        tz_name = "America/New_York"
        expected_tz_names = ["America/New_York"]

    cursor.execute(f"RETURN datetime({{year: 2025, month: 8, day: 13, hour: 14, minute: 30, second: 45, timezone: '{tz_name}'}})")
    result = cursor.fetchall()

    assert len(result) == 1

    dt = result[0][0]
    assert isinstance(dt, datetime.datetime)
    assert dt.year == 2025
    assert dt.month == 8
    assert dt.day == 13
    assert dt.hour == 14
    assert dt.minute == 30
    assert dt.second == 45
    assert str(dt.tzinfo) in expected_tz_names

