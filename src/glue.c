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

#include "glue.h"

#include "types.h"

PyObject *mg_list_to_py_tuple(const mg_list *list) {
  PyObject *tuple = PyTuple_New(mg_list_size(list));
  if (!tuple) {
    return NULL;
  }
  for (uint32_t i = 0; i < mg_list_size(list); ++i) {
    PyObject *elem = mg_value_to_py_object(mg_list_at(list, i));
    if (!elem) {
      goto cleanup;
    }
    PyTuple_SET_ITEM(tuple, i, elem);
  }

  return tuple;

cleanup:
  Py_DECREF(tuple);
  return NULL;
}

PyObject *mg_string_to_py_unicode(const mg_string *str) {
  return PyUnicode_FromStringAndSize(mg_string_data(str), mg_string_size(str));
}

PyObject *mg_list_to_py_list(const mg_list *list) {
  PyObject *pylist = PyList_New(mg_list_size(list));
  if (!pylist) {
    return NULL;
  }
  for (uint32_t i = 0; i < mg_list_size(list); ++i) {
    PyObject *elem = mg_value_to_py_object(mg_list_at(list, i));
    if (!elem) {
      goto cleanup;
    }
    PyList_SET_ITEM(pylist, i, elem);
  }

  return pylist;

cleanup:
  Py_DECREF(pylist);
  return NULL;
}

PyObject *mg_map_to_py_dict(const mg_map *map) {
  PyObject *dict = PyDict_New();
  if (!dict) {
    return NULL;
  }
  for (uint32_t i = 0; i < mg_map_size(map); ++i) {
    PyObject *key = mg_string_to_py_unicode(mg_map_key_at(map, i));
    PyObject *value = mg_value_to_py_object(mg_map_value_at(map, i));
    if (!key || !value || PyDict_SetItem(dict, key, value) < 0) {
      Py_XDECREF(key);
      Py_XDECREF(value);
      goto cleanup;
    }
  }

  return dict;

cleanup:
  Py_DECREF(dict);
  return NULL;
}

PyObject *mg_node_to_py_node(const mg_node *node) {
  PyObject *label_list = NULL;
  PyObject *label_set = NULL;
  PyObject *props = NULL;
  PyObject *ret = NULL;

  if (!(label_list = PyList_New(mg_node_label_count(node)))) {
    goto exit;
  }
  for (uint32_t i = 0; i < mg_node_label_count(node); ++i) {
    PyObject *label = mg_string_to_py_unicode(mg_node_label_at(node, i));
    if (!label) {
      goto exit;
    }
    PyList_SET_ITEM(label_list, i, label);
  }

  if (!(label_set = PySet_New(label_list))) {
    goto exit;
  }

  if (!(props = mg_map_to_py_dict(mg_node_properties(node)))) {
    goto exit;
  }

  ret = PyObject_CallFunction((PyObject *)&NodeType, "LOO", mg_node_id(node),
                              label_set, props);

exit:
  Py_XDECREF(label_list);
  Py_XDECREF(label_set);
  Py_XDECREF(props);
  return ret;
}

PyObject *mg_relationship_to_py_relationship(const mg_relationship *rel) {
  PyObject *type = NULL;
  PyObject *props = NULL;
  PyObject *ret = NULL;

  if (!(type = mg_string_to_py_unicode(mg_relationship_type(rel)))) {
    goto exit;
  }
  if (!(props = mg_map_to_py_dict(mg_relationship_properties(rel)))) {
    goto exit;
  }

  ret = PyObject_CallFunction(
      (PyObject *)&RelationshipType, "LLLOO", mg_relationship_id(rel),
      mg_relationship_start_id(rel), mg_relationship_end_id(rel), type, props);

exit:
  Py_XDECREF(type);
  Py_XDECREF(props);
  return ret;
}

PyObject *mg_unbound_relationship_to_py_relationship(
    const mg_unbound_relationship *rel) {
  PyObject *type = NULL;
  PyObject *props = NULL;
  PyObject *ret = NULL;

  if (!(type = mg_string_to_py_unicode(mg_unbound_relationship_type(rel)))) {
    goto exit;
  }
  if (!(props = mg_map_to_py_dict(mg_unbound_relationship_properties(rel)))) {
    goto exit;
  }

  ret = PyObject_CallFunction((PyObject *)&RelationshipType, "LLLOO",
                              mg_unbound_relationship_id(rel), -1, -1, type,
                              props);

exit:
  Py_XDECREF(type);
  Py_XDECREF(props);
  return ret;
}

PyObject *mg_path_to_py_path(const mg_path *path) {
  PyObject *nodes = NULL;
  PyObject *rels = NULL;
  PyObject *ret = NULL;

  if (!(nodes = PyList_New(mg_path_length(path) + 1))) {
    goto exit;
  }
  if (!(rels = PyList_New(mg_path_length(path)))) {
    goto exit;
  }

  int64_t prev_node_id = -1;
  for (uint32_t i = 0; i <= mg_path_length(path); ++i) {
    int64_t curr_node_id = mg_node_id(mg_path_node_at(path, i));
    PyObject *node = mg_node_to_py_node(mg_path_node_at(path, i));
    if (!node) {
      goto exit;
    }
    PyList_SET_ITEM(nodes, i, node);
    if (i > 0) {
      PyObject *rel = mg_unbound_relationship_to_py_relationship(
          mg_path_relationship_at(path, i - 1));
      if (!rel) {
        goto exit;
      }
      if (mg_path_relationship_reversed_at(path, i - 1)) {
        ((RelationshipObject *)rel)->start_id = curr_node_id;
        ((RelationshipObject *)rel)->end_id = prev_node_id;
      } else {
        ((RelationshipObject *)rel)->start_id = prev_node_id;
        ((RelationshipObject *)rel)->end_id = curr_node_id;
      }
      PyList_SET_ITEM(rels, i - 1, rel);
    }
    prev_node_id = curr_node_id;
  }

  ret = PyObject_CallFunction((PyObject *)&PathType, "OO", nodes, rels);

exit:
  Py_XDECREF(nodes);
  Py_XDECREF(rels);
  return ret;
}

PyObject *mg_value_to_py_object(const mg_value *value) {
  switch (mg_value_get_type(value)) {
    case MG_VALUE_TYPE_NULL:
      Py_RETURN_NONE;
    case MG_VALUE_TYPE_BOOL:
      if (mg_value_bool(value)) {
        Py_RETURN_TRUE;
      } else {
        Py_RETURN_FALSE;
      }
    case MG_VALUE_TYPE_INTEGER:
      return PyLong_FromLongLong(mg_value_integer(value));
    case MG_VALUE_TYPE_FLOAT:
      return PyFloat_FromDouble(mg_value_float(value));
    case MG_VALUE_TYPE_STRING:
      return mg_string_to_py_unicode(mg_value_string(value));
    case MG_VALUE_TYPE_LIST:
      return mg_list_to_py_list(mg_value_list(value));
    case MG_VALUE_TYPE_MAP:
      return mg_map_to_py_dict(mg_value_map(value));
    case MG_VALUE_TYPE_NODE:
      return mg_node_to_py_node(mg_value_node(value));
    case MG_VALUE_TYPE_RELATIONSHIP:
      return mg_relationship_to_py_relationship(mg_value_relationship(value));
    case MG_VALUE_TYPE_UNBOUND_RELATIONSHIP:
      return mg_unbound_relationship_to_py_relationship(
          mg_value_unbound_relationship(value));
    case MG_VALUE_TYPE_PATH:
      return mg_path_to_py_path(mg_value_path(value));
    default:
      PyErr_SetString(PyExc_RuntimeError,
                      "encountered a mg_value of unknown type");
      return NULL;
  }
}

mg_string *py_unicode_to_mg_string(PyObject *unicode) {
  assert(PyUnicode_Check(unicode));
  Py_ssize_t size;
  const char *data = PyUnicode_AsUTF8AndSize(unicode, &size);
  if (!data) {
    return NULL;
  }
  if (size > UINT32_MAX) {
    PyErr_SetString(PyExc_ValueError, "dictionary size exceeded");
    return NULL;
  }
  mg_string *ret = mg_string_make2((uint32_t)size, data);
  if (!ret) {
    PyErr_SetString(PyExc_RuntimeError, "failed to create a mg_string");
    return NULL;
  }
  return ret;
}

mg_list *py_list_to_mg_list(PyObject *pylist) {
  assert(PyList_Check(pylist));

  mg_list *list = NULL;

  if (PyList_Size(pylist) > UINT32_MAX) {
    PyErr_SetString(PyExc_ValueError, "list size exceeded");
    goto cleanup;
  }

  list = mg_list_make_empty((uint32_t)PyList_Size(pylist));
  if (!list) {
    PyErr_SetString(PyExc_RuntimeError, "failed to create a mg_list");
    goto cleanup;
  }

  for (uint32_t i = 0; i < (uint32_t)PyList_Size(pylist); ++i) {
    mg_value *elem = py_object_to_mg_value(PyList_GetItem(pylist, i));
    if (!elem) {
      return NULL;
    }
    if (mg_list_append(list, elem) != 0) {
      abort();
    }
  }

  return list;

cleanup:
  mg_list_destroy(list);
  return NULL;
}

mg_map *py_dict_to_mg_map(PyObject *dict) {
  assert(PyDict_Check(dict));

  mg_map *map = NULL;

  if (PyDict_Size(dict) > UINT32_MAX) {
    PyErr_SetString(PyExc_ValueError, "dictionary size exceeded");
    goto cleanup;
  }

  map = mg_map_make_empty((uint32_t)PyDict_Size(dict));
  if (!map) {
    PyErr_SetString(PyExc_RuntimeError, "failed to create a mg_map");
    goto cleanup;
  }

  Py_ssize_t pos = 0;
  PyObject *pykey;
  PyObject *pyvalue;
  while (PyDict_Next(dict, &pos, &pykey, &pyvalue)) {
    if (!PyUnicode_Check(pykey)) {
      PyErr_SetString(PyExc_ValueError, "dictionary key must be a string");
      goto cleanup;
    }
    mg_string *key = py_unicode_to_mg_string(pykey);
    if (!key) {
      goto cleanup;
    }

    mg_value *value = py_object_to_mg_value(pyvalue);
    if (!value) {
      mg_string_destroy(key);
      goto cleanup;
    }

    if (mg_map_insert_unsafe2(map, key, value) != 0) {
      abort();
    }
  }

  return map;

cleanup:
  mg_map_destroy(map);
  return NULL;
}

mg_value *py_object_to_mg_value(PyObject *object) {
  mg_value *ret = NULL;

  if (object == Py_None) {
    ret = mg_value_make_null();
  } else if (PyBool_Check(object)) {
    ret = mg_value_make_bool(object == Py_True);
  } else if (PyLong_Check(object)) {
    int64_t as_int64 = PyLong_AsLongLong(object);
    if (as_int64 == -1 && PyErr_Occurred()) {
      return NULL;
    }
    ret = mg_value_make_integer(as_int64);
  } else if (PyFloat_Check(object)) {
    double as_double = PyFloat_AsDouble(object);
    if (as_double == -1.0 && PyErr_Occurred()) {
      return NULL;
    }
    ret = mg_value_make_float(as_double);
  } else if (PyUnicode_Check(object)) {
    mg_string *str = py_unicode_to_mg_string(object);
    if (!str) {
      return NULL;
    }
    ret = mg_value_make_string2(str);
  } else if (PyList_Check(object)) {
    mg_list *list = py_list_to_mg_list(object);
    if (!list) {
      return NULL;
    }
    ret = mg_value_make_list(list);
  } else if (PyDict_Check(object)) {
    mg_map *map = py_dict_to_mg_map(object);
    if (!map) {
      return NULL;
    }
    ret = mg_value_make_map(map);
  } else {
    PyErr_Format(PyExc_ValueError,
                 "value of type '%s' can't be used as query parameter",
                 Py_TYPE(object)->tp_name);
    return NULL;
  }

  if (!ret) {
    PyErr_SetString(PyExc_RuntimeError, "failed to create a mg_value");
    return NULL;
  }

  return ret;
}
