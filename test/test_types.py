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

import mgclient


def test_node():
    node1 = mgclient.Node(1, set(), {})
    assert str(node1) == "()"

    node2 = mgclient.Node(1, set(["Label1"]), {})
    assert str(node2) == "(:Label1)"

    node3 = mgclient.Node(1, set(["Label2"]), {'prop': 1})
    assert str(node3) == "(:Label2 {'prop': 1})"

    node4 = mgclient.Node(1, set(), {'prop': 1})
    assert str(node4) == "({'prop': 1})"


def test_relationship():
    rel1 = mgclient.Relationship(0, 1, 2, "Type", {})
    assert str(rel1) == "[:Type]"

    rel2 = mgclient.Relationship(0, 1, 2, "Type", {'prop': 1})
    assert str(rel2) == "[:Type {'prop': 1}]"


def test_path():
    n1 = mgclient.Node(1, set(["Label1"]), {})
    n2 = mgclient.Node(2, set(["Label2"]), {})
    n3 = mgclient.Node(3, set(["Label3"]), {})

    e1 = mgclient.Relationship(1, 1, 2, "Edge1", {})
    e2 = mgclient.Relationship(2, 3, 2, "Edge2", {})

    path = mgclient.Path([n1, n2, n3], [e1, e2])
    assert str(path) == "(:Label1)-[:Edge1]->(:Label2)<-[:Edge2]-(:Label3)"
