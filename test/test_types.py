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

import pytest

import mgclient


def test_node():
    node1 = mgclient.Node(1, set(), {})
    assert str(node1) == "()"

    node2 = mgclient.Node(1, set(["Label1"]), {})
    assert str(node2) == "(:Label1)"

    node3 = mgclient.Node(1, set(["Label2"]), {"prop": 1})
    assert str(node3) == "(:Label2 {'prop': 1})"

    node4 = mgclient.Node(1, set(), {"prop": 1})
    assert str(node4) == "({'prop': 1})"


def test_relationship():
    rel1 = mgclient.Relationship(0, 1, 2, "Type", {})
    assert str(rel1) == "[:Type]"

    rel2 = mgclient.Relationship(0, 1, 2, "Type", {"prop": 1})
    assert str(rel2) == "[:Type {'prop': 1}]"


def test_path():
    n1 = mgclient.Node(1, set(["Label1"]), {})
    n2 = mgclient.Node(2, set(["Label2"]), {})
    n3 = mgclient.Node(3, set(["Label3"]), {})

    e1 = mgclient.Relationship(1, 1, 2, "Edge1", {})
    e2 = mgclient.Relationship(2, 3, 2, "Edge2", {})

    path = mgclient.Path([n1, n2, n3], [e1, e2])
    assert str(path) == "(:Label1)-[:Edge1]->(:Label2)<-[:Edge2]-(:Label3)"


def test_point_2d_attributes():
    point = mgclient.Point2D(7203, 1.5, 2.5)
    assert point.srid == 7203
    assert point.x_longitude == 1.5
    assert point.y_latitude == 2.5


def test_point_3d_attributes():
    point = mgclient.Point3D(9157, 1.5, 2.5, 3.5)
    assert point.srid == 9157
    assert point.x_longitude == 1.5
    assert point.y_latitude == 2.5
    assert point.z_height == 3.5


def test_point_accepts_keyword_arguments():
    assert mgclient.Point2D(
        srid=7203, x_longitude=1.5, y_latitude=2.5
    ) == mgclient.Point2D(7203, 1.5, 2.5)
    assert mgclient.Point3D(
        srid=9157, x_longitude=1.5, y_latitude=2.5, z_height=3.5
    ) == mgclient.Point3D(9157, 1.5, 2.5, 3.5)


def test_point_attributes_are_readonly():
    point = mgclient.Point2D(7203, 1.5, 2.5)
    with pytest.raises(AttributeError):
        point.srid = 4326
    with pytest.raises(AttributeError):
        mgclient.Point3D(9157, 1.5, 2.5, 3.5).z_height = 0.0


def test_point_accepts_integer_coordinates():
    point = mgclient.Point2D(7203, 1, 2)
    assert point.x_longitude == 1.0
    assert point.y_latitude == 2.0


def test_point_str():
    assert str(mgclient.Point2D(7203, 1.5, 2.5)) == "POINT({x: 1.5, y: 2.5, srid: 7203})"
    assert (
        str(mgclient.Point3D(9157, 1.5, 2.5, 3.5))
        == "POINT({x: 1.5, y: 2.5, z: 3.5, srid: 9157})"
    )


def test_point_repr():
    assert repr(mgclient.Point2D(7203, 1.5, 2.5)).startswith(
        "<mgclient.Point2D(srid=7203, x_longitude=1.5, y_latitude=2.5) at "
    )
    assert repr(mgclient.Point3D(9157, 1.5, 2.5, 3.5)).startswith(
        "<mgclient.Point3D(srid=9157, x_longitude=1.5, y_latitude=2.5, z_height=3.5) at "
    )


def test_point_equality():
    assert mgclient.Point2D(7203, 1.5, 2.5) == mgclient.Point2D(7203, 1.5, 2.5)
    assert mgclient.Point3D(9157, 1.5, 2.5, 3.5) == mgclient.Point3D(9157, 1.5, 2.5, 3.5)


def test_points_differing_in_any_field_are_unequal():
    reference = mgclient.Point3D(9157, 1.5, 2.5, 3.5)
    assert reference != mgclient.Point3D(4979, 1.5, 2.5, 3.5)
    assert reference != mgclient.Point3D(9157, 9.9, 2.5, 3.5)
    assert reference != mgclient.Point3D(9157, 1.5, 9.9, 3.5)
    assert reference != mgclient.Point3D(9157, 1.5, 2.5, 9.9)


def test_point_2d_and_3d_are_never_equal():
    assert mgclient.Point2D(7203, 1.5, 2.5) != mgclient.Point3D(7203, 1.5, 2.5, 0.0)


def test_point_unequal_to_other_types():
    assert mgclient.Point2D(7203, 1.5, 2.5) != "POINT({x: 1.5, y: 2.5, srid: 7203})"
    assert mgclient.Point2D(7203, 1.5, 2.5) != None  # noqa: E711


def test_equal_points_hash_equally():
    points = {mgclient.Point2D(7203, 1.5, 2.5), mgclient.Point2D(7203, 1.5, 2.5)}
    assert len(points) == 1
    assert hash(mgclient.Point3D(9157, 1.5, 2.5, 3.5)) == hash(
        mgclient.Point3D(9157, 1.5, 2.5, 3.5)
    )


def test_point_rejects_bad_arguments():
    with pytest.raises(TypeError):
        mgclient.Point2D(7203, 1.5)
    with pytest.raises(TypeError):
        mgclient.Point2D(7203, 1.5, "not a number")
    with pytest.raises(TypeError):
        mgclient.Point3D(9157, 1.5, 2.5)


def test_point_rejects_out_of_range_srid():
    with pytest.raises(OverflowError):
        mgclient.Point2D(-1, 1.5, 2.5)
    with pytest.raises(OverflowError):
        mgclient.Point2D(70000, 1.5, 2.5)
    with pytest.raises(OverflowError):
        mgclient.Point3D(-1, 1.5, 2.5, 3.5)


def test_point_accepts_srid_at_the_limits_of_the_wire_format():
    assert mgclient.Point2D(0, 1.5, 2.5).srid == 0
    assert mgclient.Point2D(65535, 1.5, 2.5).srid == 65535
    assert mgclient.Point3D(65535, 1.5, 2.5, 3.5).srid == 65535
