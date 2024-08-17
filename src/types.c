// Copyright (c) 2016-2020 Memgraph Ltd. [https://memgraph.com]
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "types.h"

#include <structmember.h>

PyTypeObject NodeType;
PyTypeObject RelationshipType;
PyTypeObject PathType;

#define CHECK_ATTRIBUTE(obj, name)                                            \
  do {                                                                        \
    if (!obj->name) {                                                         \
      PyErr_SetString(PyExc_AttributeError, "attribute '" #name "' is NULL"); \
      return NULL;                                                            \
    }                                                                         \
  } while (0)

static void node_dealloc(NodeObject *node) {
  Py_CLEAR(node->labels);
  Py_CLEAR(node->properties);
  Py_TYPE(node)->tp_free(node);
}

static PyObject *node_repr(NodeObject *node) {
  return PyUnicode_FromFormat("<%s(id=%lld, labels=%R, properties=%R) at %p>",
                              Py_TYPE(node)->tp_name, node->id, node->labels,
                              node->properties, node);
}

static PyObject *node_str(NodeObject *node) {
  CHECK_ATTRIBUTE(node, labels);
  CHECK_ATTRIBUTE(node, properties);

  if (PySet_Size(node->labels)) {
    PyObject *colon = PyUnicode_FromString(":");
    if (!colon) {
      return NULL;
    }
    PyObject *labels = PyUnicode_Join(colon, node->labels);
    Py_DECREF(colon);
    if (!labels) {
      return NULL;
    }
    PyObject *result =
        PyDict_Size(node->properties)
            ? PyUnicode_FromFormat("(:%S %S)", labels, node->properties)
            : PyUnicode_FromFormat("(:%S)", labels);
    Py_DECREF(labels);
    return result;
  } else {
    if (PyDict_Size(node->properties)) {
      return PyUnicode_FromFormat("(%S)", node->properties);
    } else {
      return PyUnicode_FromString("()");
    }
  }
}

// Helper function for implementing richcompare.
static PyObject *node_astuple(NodeObject *node) {
  CHECK_ATTRIBUTE(node, labels);
  CHECK_ATTRIBUTE(node, properties);

  PyObject *tuple = PyTuple_New(3);
  if (!tuple) {
    return NULL;
  }

  PyObject *id = PyLong_FromLongLong(node->id);
  if (!id) {
    Py_DECREF(tuple);
    return NULL;
  }
  Py_INCREF(node->labels);
  Py_INCREF(node->properties);

  PyTuple_SET_ITEM(tuple, 0, id);
  PyTuple_SET_ITEM(tuple, 1, node->labels);
  PyTuple_SET_ITEM(tuple, 2, node->properties);
  return tuple;
}

static PyObject *node_richcompare(NodeObject *lhs, PyObject *rhs, int op) {
  PyObject *tlhs = NULL;
  PyObject *trhs = NULL;
  PyObject *ret = NULL;

  if (Py_TYPE(rhs) == &NodeType) {
    if (!(tlhs = node_astuple(lhs))) {
      goto exit;
    }
    if (!(trhs = node_astuple((NodeObject *)rhs))) {
      goto exit;
    }
    ret = PyObject_RichCompare(tlhs, trhs, op);
  } else {
    Py_INCREF(Py_False);
    ret = Py_False;
  }

exit:
  Py_XDECREF(tlhs);
  Py_XDECREF(trhs);
  return ret;
}

int node_init(NodeObject *node, PyObject *args, PyObject *kwargs) {
  int64_t id = -1;
  PyObject *labels;
  PyObject *properties;

  static char *kwlist[] = {"", "", "", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "LOO", kwlist, &id, &labels,
                                   &properties)) {
    return -1;
  }

  if (!PySet_Check(labels)) {
    PyErr_SetString(PyExc_TypeError, "__init__ argument 2 must be a set");
    return -1;
  }
  if (!PyDict_Check(properties)) {
    PyErr_SetString(PyExc_TypeError, "__init__ argument 3 must be a dict");
    return -1;
  }

  node->id = id;

  PyObject *tmp_labels = node->labels;
  Py_INCREF(labels);
  node->labels = labels;
  Py_XDECREF(tmp_labels);

  PyObject *tmp_properties = node->properties;
  Py_INCREF(properties);
  node->properties = properties;
  Py_XDECREF(tmp_properties);

  return 0;
}

PyDoc_STRVAR(NodeType_id_doc,
             "Unique node identifier (within the scope of its origin graph).");

PyDoc_STRVAR(NodeType_labels_doc, "A list of node labels.");

PyDoc_STRVAR(NodeType_properties_doc, "A dictionary of node properties.");

static PyMemberDef node_members[] = {
    {"id", T_LONGLONG, offsetof(NodeObject, id), READONLY, NodeType_id_doc},
    {"labels", T_OBJECT_EX, offsetof(NodeObject, labels), READONLY,
     NodeType_labels_doc},
    {"properties", T_OBJECT_EX, offsetof(NodeObject, properties), READONLY,
     NodeType_properties_doc},
    {NULL}};

PyDoc_STRVAR(NodeType_doc,
             "A node in the graph with optional properties and labels.");

// clang-format off
PyTypeObject NodeType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "mgclient.Node",
    .tp_basicsize = sizeof(NodeObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)node_dealloc,
    .tp_repr = (reprfunc)node_repr,
    .tp_str = (reprfunc)node_str,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = NodeType_doc,
    .tp_richcompare = (richcmpfunc)node_richcompare,
    .tp_members = node_members,
    .tp_init = (initproc)node_init,
    .tp_new = PyType_GenericNew
};
// clang-format on

static void relationship_dealloc(RelationshipObject *rel) {
  Py_CLEAR(rel->type);
  Py_CLEAR(rel->properties);
  Py_TYPE(rel)->tp_free(rel);
}

static PyObject *relationship_repr(RelationshipObject *rel) {
  return PyUnicode_FromFormat(
      "<%s(start_id=%lld, end_id=%lld, type=%R, properties=%R) at %p>",
      Py_TYPE(rel)->tp_name, rel->start_id, rel->end_id, rel->type,
      rel->properties, rel);
}

static PyObject *relationship_str(RelationshipObject *rel) {
  CHECK_ATTRIBUTE(rel, type);
  CHECK_ATTRIBUTE(rel, properties);

  if (PyDict_Size(rel->properties)) {
    return PyUnicode_FromFormat("[:%S %S]", rel->type, rel->properties);
  } else {
    return PyUnicode_FromFormat("[:%S]", rel->type);
  }
}

// Helper function for implementing richcompare.
static PyObject *relationship_astuple(RelationshipObject *rel) {
  CHECK_ATTRIBUTE(rel, type);
  CHECK_ATTRIBUTE(rel, properties);

  PyObject *id = NULL;
  PyObject *start_id = NULL;
  PyObject *end_id = NULL;
  PyObject *tuple = NULL;

  if (!(id = PyLong_FromLongLong(rel->id))) {
    goto cleanup;
  }
  if (!(start_id = PyLong_FromLongLong(rel->start_id))) {
    goto cleanup;
  }
  if (!(end_id = PyLong_FromLongLong(rel->end_id))) {
    goto cleanup;
  }
  if (!(tuple = PyTuple_New(5))) {
    goto cleanup;
  }

  PyTuple_SET_ITEM(tuple, 0, id);
  PyTuple_SET_ITEM(tuple, 1, start_id);
  PyTuple_SET_ITEM(tuple, 2, end_id);
  Py_INCREF(rel->type);
  PyTuple_SET_ITEM(tuple, 3, rel->type);
  Py_INCREF(rel->properties);
  PyTuple_SET_ITEM(tuple, 4, rel->properties);

  return tuple;

cleanup:
  Py_XDECREF(id);
  Py_XDECREF(start_id);
  Py_XDECREF(end_id);
  Py_XDECREF(tuple);
  return NULL;
}

static PyObject *relationship_richcompare(RelationshipObject *lhs,
                                          PyObject *rhs, int op) {
  PyObject *tlhs = NULL;
  PyObject *trhs = NULL;
  PyObject *ret = NULL;

  if (Py_TYPE(rhs) == &RelationshipType) {
    if (!(tlhs = relationship_astuple(lhs))) {
      goto exit;
    }
    if (!(trhs = relationship_astuple((RelationshipObject *)rhs))) {
      goto exit;
    }
    ret = PyObject_RichCompare(tlhs, trhs, op);
  } else {
    Py_INCREF(Py_False);
    ret = Py_False;
  }

exit:
  Py_XDECREF(tlhs);
  Py_XDECREF(trhs);
  return ret;
}

int relationship_init(RelationshipObject *rel, PyObject *args,
                      PyObject *kwargs) {
  int64_t id;
  int64_t start_id = -1;
  int64_t end_id = -1;
  PyObject *type;
  PyObject *properties;

  static char *kwlist[] = {"", "", "", "", "", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "LLLOO", kwlist, &id,
                                   &start_id, &end_id, &type, &properties)) {
    return -1;
  }

  if (!PyUnicode_Check(type)) {
    PyErr_SetString(PyExc_TypeError, "__init__ argument 4 must be a string");
    return -1;
  }
  if (!PyDict_Check(properties)) {
    PyErr_SetString(PyExc_TypeError, "__init__ argument 5 must be a dict");
    return -1;
  }

  rel->id = id;
  rel->start_id = start_id;
  rel->end_id = end_id;

  PyObject *tmp_type = rel->type;
  Py_INCREF(type);
  rel->type = type;
  Py_XDECREF(tmp_type);

  PyObject *tmp_properties = rel->properties;
  Py_INCREF(properties);
  rel->properties = properties;
  Py_XDECREF(tmp_properties);

  return 0;
}

PyDoc_STRVAR(
    RelationshipType_id_doc,
    "Unique relationship identifier (within the scope of its origin graph).");

PyDoc_STRVAR(RelationshipType_start_id_doc,
             "Identifier of relationship start node (or -1 if it was not "
             "supplied by the database).");

PyDoc_STRVAR(RelationshipType_end_id_doc,
             "Identifier of relationship end node (or -1 if it was not "
             "supplied by the database).");

PyDoc_STRVAR(RelationshipType_type_doc, "Relationship type.");

PyDoc_STRVAR(RelationshipType_properties_doc,
             "A dictionary of relationship properties.");

static PyMemberDef relationship_members[] = {
    {"id", T_LONGLONG, offsetof(RelationshipObject, id), READONLY,
     RelationshipType_id_doc},
    {"start_id", T_LONGLONG, offsetof(RelationshipObject, start_id), READONLY,
     RelationshipType_start_id_doc},
    {"end_id", T_LONGLONG, offsetof(RelationshipObject, end_id), READONLY,
     RelationshipType_end_id_doc},
    {"type", T_OBJECT_EX, offsetof(RelationshipObject, type), READONLY,
     RelationshipType_type_doc},
    {"properties", T_OBJECT_EX, offsetof(RelationshipObject, properties),
     READONLY, RelationshipType_properties_doc},
    {NULL}};

PyDoc_STRVAR(
    RelationshipType_doc,
    "A directed, typed connection between two nodes with optional properties.");

// clang-format off
 PyTypeObject RelationshipType = {
     PyVarObject_HEAD_INIT(NULL, 0)
     .tp_name = "mgclient.Relationship",
     .tp_basicsize = sizeof(RelationshipObject),
     .tp_itemsize = 0,
     .tp_dealloc = (destructor)relationship_dealloc,
     .tp_repr = (reprfunc)relationship_repr,
     .tp_str = (reprfunc)relationship_str,
     .tp_flags = Py_TPFLAGS_DEFAULT,
     .tp_doc = RelationshipType_doc,
     .tp_richcompare = (richcmpfunc)relationship_richcompare,
     .tp_members = relationship_members,
     .tp_init = (initproc)relationship_init,
     .tp_new = PyType_GenericNew
 };
// clang-format on

static void path_dealloc(PathObject *path) {
  Py_CLEAR(path->nodes);
  Py_CLEAR(path->relationships);
  Py_TYPE(path)->tp_free(path);
}

static PyObject *path_repr(PathObject *path) {
  return PyUnicode_FromFormat("<%s(nodes=%R, relationships=%R) at %p>",
                              Py_TYPE(path)->tp_name, path->nodes,
                              path->relationships, path);
}

static PyObject *path_str(PathObject *path) {
  CHECK_ATTRIBUTE(path, nodes);
  CHECK_ATTRIBUTE(path, relationships);

  PyObject *result = NULL;

  Py_ssize_t length = PyList_Size(path->relationships);
  PyObject *elements = PyList_New(2 * length + 1);
  if (!elements) {
    goto end;
  }

  for (Py_ssize_t i = 0; i <= length; ++i) {
    NodeObject *node = (NodeObject *)PyList_GetItem(path->nodes, i);
    if (!node) {
      goto end;
    }
    PyObject *node_s = node_str(node);
    if (!node_s) {
      goto end;
    }
    PyList_SET_ITEM(elements, 2 * i, node_s);
    if (i < length) {
      RelationshipObject *rel =
          (RelationshipObject *)PyList_GetItem(path->relationships, i);
      PyObject *rel_s;
      if (rel->start_id == node->id) {
        rel_s = PyUnicode_FromFormat("-%S->", rel);
      } else {
        rel_s = PyUnicode_FromFormat("<-%S-", rel);
      }
      if (!rel_s) {
        goto end;
      }
      PyList_SET_ITEM(elements, 2 * i + 1, rel_s);
    }
  }

  PyObject *sep = PyUnicode_FromString("");
  if (!sep) {
    goto end;
  }
  result = PyUnicode_Join(sep, elements);
  Py_DECREF(sep);

end:
  Py_XDECREF(elements);
  return result;
}

// Helper function for implementing richcompare.
static PyObject *path_astuple(PathObject *path) {
  CHECK_ATTRIBUTE(path, nodes);
  CHECK_ATTRIBUTE(path, relationships);

  PyObject *tuple = PyTuple_New(2);
  if (!tuple) {
    return NULL;
  }
  Py_INCREF(path->nodes);
  Py_INCREF(path->relationships);
  PyTuple_SET_ITEM(tuple, 0, path->nodes);
  PyTuple_SET_ITEM(tuple, 1, path->relationships);
  return tuple;
}

static PyObject *path_richcompare(PathObject *lhs, PathObject *rhs, int op) {
  PyObject *tlhs = NULL;
  PyObject *trhs = NULL;
  PyObject *ret = NULL;
  if (Py_TYPE(rhs) == &PathType) {
    if (!(tlhs = path_astuple(lhs))) {
      goto exit;
    }
    if (!(trhs = path_astuple((PathObject *)rhs))) {
      goto exit;
    }
    ret = PyObject_RichCompare(tlhs, trhs, op);

  } else {
    Py_INCREF(Py_False);
    ret = Py_False;
  }

exit:
  Py_XDECREF(tlhs);
  Py_XDECREF(trhs);
  return ret;
}

static int check_types_in_list(PyObject *list, PyTypeObject *expected_type,
                               const char *function_name, int arg_index) {
  int ok = 1;
  if (PyList_Check(list)) {
    PyObject *iter = PyObject_GetIter(list);
    if (!iter) {
      return -1;
    }
    PyObject *elem;
    while ((elem = PyIter_Next(iter))) {
      PyTypeObject *t = Py_TYPE(elem);
      Py_DECREF(elem);
      if (t != expected_type) {
        ok = 0;
        break;
      }
    }
    if (PyErr_Occurred()) {
      return -1;
    }
  } else {
    ok = 0;
  }
  if (!ok) {
    PyErr_Format(PyExc_TypeError, "%s argument %d must be a list of '%s'",
                 function_name, arg_index, expected_type->tp_name);
    return -1;
  }
  return 0;
}

static int path_init(PathObject *path, PyObject *args, PyObject *kwargs) {
  PyObject *nodes;
  PyObject *relationships;

  static char *kwlist[] = {"", "", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO", kwlist, &nodes,
                                   &relationships)) {
    return -1;
  }

  if (check_types_in_list(nodes, &NodeType, "__init__", 1) < 0 ||
      check_types_in_list(relationships, &RelationshipType, "__init__", 2) <
          0) {
    return -1;
  }

  PyObject *tmp_nodes = path->nodes;
  Py_INCREF(nodes);
  path->nodes = nodes;
  Py_XDECREF(tmp_nodes);

  PyObject *tmp_relationships = path->relationships;
  Py_INCREF(relationships);
  path->relationships = relationships;
  Py_XDECREF(tmp_relationships);

  return 0;
}

// clang-format off
PyDoc_STRVAR(PathType_nodes_doc,
"A list of nodes in the order they appear in the path. It has one element\n\
more than the :attr:`relationships` list.");

PyDoc_STRVAR(PathType_relationships_doc,
"A list of relationships in the order they appear in the path. It has one\n\
element less than the :attr:`nodes` list.");
// clang-format on

static PyMemberDef path_members[] = {
    {"nodes", T_OBJECT_EX, offsetof(PathObject, nodes), READONLY,
     PathType_nodes_doc},
    {"relationships", T_OBJECT_EX, offsetof(PathObject, relationships),
     READONLY, PathType_relationships_doc},
    {NULL}};

// clang-format off
PyDoc_STRVAR(PathType_doc,
"A sequence of alternating nodes and relationships corresponding to a walk\n\
in the graph.");
// clang-format on

// clang-format off
PyTypeObject PathType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "mgclient.Path",
    .tp_basicsize = sizeof(PathObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)path_dealloc,
    .tp_repr = (reprfunc)path_repr,
    .tp_str = (reprfunc)path_str,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = PathType_doc,
    .tp_richcompare = (richcmpfunc)path_richcompare,
    .tp_members = path_members,
    .tp_init = (initproc)path_init,
    .tp_new = PyType_GenericNew
};
// clang-format on

static void point2d_dealloc(Point2DObject *point2d) {
  Py_TYPE(point2d)->tp_free(point2d);
}

static PyObject *point2d_repr(Point2DObject *point2d) {
  char buffer[256];
  snprintf(buffer, sizeof(buffer), "<%s(srid=%u, x_longitude=%f, y_latitude=%f) at %p>", Py_TYPE(point2d)->tp_name, point2d->srid, point2d->x_longitude, point2d->y_latitude, point2d);
  return PyUnicode_FromFormat("%s", buffer);
}

static PyObject *point2d_str(Point2DObject *point2d) {
  // NOTE: Somehow, PyUnicode_FromFormat doesn't suppord formatting double values.
  // https://stackoverflow.com/questions/1701055/what-is-the-maximum-length-in-chars-needed-to-represent-any-double-value
  char buffer[256];
  snprintf(buffer, sizeof(buffer), "Point2D({ srid=%u, x_longitude=%f, y_latitude=%f })", point2d->srid, point2d->x_longitude, point2d->y_latitude);
  return PyUnicode_FromFormat("%s", buffer);
}

// Helper function for implementing richcompare.
static PyObject *point2d_astuple(Point2DObject *point2d) {
  PyObject *tuple = NULL;
  PyObject *srid = NULL;
  PyObject *x_longitude = NULL;
  PyObject *y_latitude = NULL;

  if (!(srid = PyLong_FromUnsignedLong(point2d->srid))) {
    goto cleanup;
  }
  if (!(x_longitude = PyFloat_FromDouble(point2d->x_longitude))) {
    goto cleanup;
  }
  if (!(y_latitude = PyFloat_FromDouble(point2d->y_latitude))) {
    goto cleanup;
  }
  if (!(tuple = PyTuple_New(3))) {
    goto cleanup;
  }

  PyTuple_SET_ITEM(tuple, 0, srid);
  PyTuple_SET_ITEM(tuple, 1, x_longitude);
  PyTuple_SET_ITEM(tuple, 2, y_latitude);
  return tuple;

cleanup:
  Py_XDECREF(tuple);
  Py_XDECREF(srid);
  Py_XDECREF(x_longitude);
  Py_XDECREF(y_latitude);
  return NULL;
}

static PyObject *point2d_richcompare(Point2DObject *lhs, PyObject *rhs, int op) {
  PyObject *tlhs = NULL;
  PyObject *trhs = NULL;
  PyObject *ret = NULL;

  if (Py_TYPE(rhs) == &Point2DType) {
    if (!(tlhs = point2d_astuple(lhs))) {
      goto exit;
    }
    if (!(trhs = point2d_astuple((Point2DObject *)rhs))) {
      goto exit;
    }
    ret = PyObject_RichCompare(tlhs, trhs, op);
  } else {
    Py_INCREF(Py_False);
    ret = Py_False;
  }

exit:
  Py_XDECREF(tlhs);
  Py_XDECREF(trhs);
  return ret;
}

int point2d_init(Point2DObject *point2d, PyObject *args, PyObject *kwargs) {
  uint16_t srid = 0;
  double x_longitude = 0;
  double y_latitude = 0;
  static char *kwlist[] = {"", "", "", NULL};
  // https://docs.python.org/3/c-api/arg.html#numbers
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Hdd", kwlist, &srid, &x_longitude, &y_latitude)) {
    return -1;
  }

  point2d->srid = srid;
  point2d->x_longitude = x_longitude;
  point2d->y_latitude = y_latitude;
  return 0;
}

PyDoc_STRVAR(Point2DType_srid_doc,
             "Point2D srid (a unique identifier associated with a specific coordinate system, tolerance, and resolution).");
PyDoc_STRVAR(Point2DType_x_longitude_doc, "Point2D x or longitude value.");
PyDoc_STRVAR(Point2DType_y_latitude_doc, "Point2D y or latitude value.");
static PyMemberDef point2d_members[] = {
    {"srid", T_USHORT, offsetof(Point2DObject, srid), READONLY, Point2DType_srid_doc},
    {"x_longitude", T_DOUBLE, offsetof(Point2DObject, x_longitude), READONLY, Point2DType_x_longitude_doc},
    {"y_latitude", T_DOUBLE, offsetof(Point2DObject, y_latitude), READONLY, Point2DType_y_latitude_doc},
    {NULL}};

PyDoc_STRVAR(Point2DType_doc,
             "A Point2D object.");
// clang-format off
PyTypeObject Point2DType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "mgclient.Point2D",
    .tp_basicsize = sizeof(Point2DObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)point2d_dealloc,
    .tp_repr = (reprfunc)point2d_repr,
    .tp_str = (reprfunc)point2d_str,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = Point2DType_doc,
    .tp_richcompare = (richcmpfunc)point2d_richcompare,
    .tp_members = point2d_members,
    .tp_init = (initproc)point2d_init,
    .tp_new = PyType_GenericNew
};
// clang-format on

static void point3d_dealloc(Point3DObject *point3d) {
  Py_TYPE(point3d)->tp_free(point3d);
}

static PyObject *point3d_repr(Point3DObject *point3d) {
  char buffer[256];
  snprintf(buffer, sizeof(buffer), "<%s(srid=%u, x_longitude=%f, y_latitude=%f, z_height=%f) at %p>", Py_TYPE(point3d)->tp_name, point3d->srid, point3d->x_longitude, point3d->y_latitude, point3d->z_height, point3d);
  return PyUnicode_FromFormat("%s", buffer);
}

static PyObject *point3d_str(Point3DObject *point3d) {
  // NOTE: Somehow, PyUnicode_FromFormat doesn't suppord formatting double values.
  // https://stackoverflow.com/questions/1701055/what-is-the-maximum-length-in-chars-needed-to-represent-any-double-value
  char buffer[256];
  snprintf(buffer, sizeof(buffer), "Point3D({ srid=%u, x_longitude=%f, y_latitude=%f, z_height=%f })", point3d->srid, point3d->x_longitude, point3d->y_latitude, point3d->z_height);
  return PyUnicode_FromFormat("%s", buffer);
}

// Helper function for implementing richcompare.
static PyObject *point3d_astuple(Point3DObject *point3d) {
  PyObject *tuple = NULL;
  PyObject *srid = NULL;
  PyObject *x_longitude = NULL;
  PyObject *y_latitude = NULL;
  PyObject *z_height = NULL;

  if (!(srid = PyLong_FromUnsignedLong(point3d->srid))) {
    goto cleanup;
  }
  if (!(x_longitude = PyFloat_FromDouble(point3d->x_longitude))) {
    goto cleanup;
  }
  if (!(y_latitude = PyFloat_FromDouble(point3d->y_latitude))) {
    goto cleanup;
  }
  if (!(z_height = PyFloat_FromDouble(point3d->z_height))) {
    goto cleanup;
  }
  if (!(tuple = PyTuple_New(4))) {
    goto cleanup;
  }

  PyTuple_SET_ITEM(tuple, 0, srid);
  PyTuple_SET_ITEM(tuple, 1, x_longitude);
  PyTuple_SET_ITEM(tuple, 2, y_latitude);
  PyTuple_SET_ITEM(tuple, 3, z_height);
  return tuple;

cleanup:
  Py_XDECREF(tuple);
  Py_XDECREF(srid);
  Py_XDECREF(x_longitude);
  Py_XDECREF(y_latitude);
  Py_XDECREF(z_height);
  return NULL;
}

static PyObject *point3d_richcompare(Point3DObject *lhs, PyObject *rhs, int op) {
  PyObject *tlhs = NULL;
  PyObject *trhs = NULL;
  PyObject *ret = NULL;

  if (Py_TYPE(rhs) == &Point3DType) {
    if (!(tlhs = point3d_astuple(lhs))) {
      goto exit;
    }
    if (!(trhs = point3d_astuple((Point3DObject *)rhs))) {
      goto exit;
    }
    ret = PyObject_RichCompare(tlhs, trhs, op);
  } else {
    Py_INCREF(Py_False);
    ret = Py_False;
  }

exit:
  Py_XDECREF(tlhs);
  Py_XDECREF(trhs);
  return ret;
}

int point3d_init(Point3DObject *point3d, PyObject *args, PyObject *kwargs) {
  uint16_t srid = 0;
  double x_longitude = 0;
  double y_latitude = 0;
  double z_height = 0;
  static char *kwlist[] = {"", "", "", "", NULL};
  // https://docs.python.org/3/c-api/arg.html#numbers
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Hddd", kwlist, &srid, &x_longitude, &y_latitude, &z_height)) {
    return -1;
  }

  point3d->srid = srid;
  point3d->x_longitude = x_longitude;
  point3d->y_latitude = y_latitude;
  point3d->z_height = z_height;
  return 0;
}

PyDoc_STRVAR(Point3DType_srid_doc,
             "Point3D srid (a unique identifier associated with a specific coordinate system, tolerance, and resolution).");
PyDoc_STRVAR(Point3DType_x_longitude_doc, "Point3D x or longitude value.");
PyDoc_STRVAR(Point3DType_y_latitude_doc, "Point3D y or latitude value.");
PyDoc_STRVAR(Point3DType_z_height_doc, "Point3D z or height value.");
static PyMemberDef point3d_members[] = {
    {"srid", T_USHORT, offsetof(Point3DObject, srid), READONLY, Point3DType_srid_doc},
    {"x_longitude", T_DOUBLE, offsetof(Point3DObject, x_longitude), READONLY, Point3DType_x_longitude_doc},
    {"y_latitude", T_DOUBLE, offsetof(Point3DObject, y_latitude), READONLY, Point3DType_y_latitude_doc},
    {"z_height", T_DOUBLE, offsetof(Point3DObject, z_height), READONLY, Point3DType_z_height_doc},
    {NULL}};

PyDoc_STRVAR(Point3DType_doc,
             "A Point3D object.");
// clang-format off
PyTypeObject Point3DType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "mgclient.Point3D",
    .tp_basicsize = sizeof(Point3DObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)point3d_dealloc,
    .tp_repr = (reprfunc)point3d_repr,
    .tp_str = (reprfunc)point3d_str,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = Point3DType_doc,
    .tp_richcompare = (richcmpfunc)point3d_richcompare,
    .tp_members = point3d_members,
    .tp_init = (initproc)point3d_init,
    .tp_new = PyType_GenericNew
};
// clang-format on

#undef CHECK_ATTRIBUTE
