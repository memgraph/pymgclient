// Copyright (c) 2016-2019 Memgraph Ltd. [https://memgraph.com]
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

#include "column.h"

#include <structmember.h>

static void column_dealloc(ColumnObject *column) {
  Py_XDECREF(column->name);
  Py_XDECREF(column->type_code);
  Py_XDECREF(column->display_size);
  Py_XDECREF(column->internal_size);
  Py_XDECREF(column->precision);
  Py_XDECREF(column->scale);
  Py_XDECREF(column->null_ok);
  Py_TYPE(column)->tp_free(column);
}

static PyObject *column_repr(ColumnObject *column) {
  return PyUnicode_FromFormat(
      "<%s(name=%R, type_code=%R, display_size=%R, internal_size=%R, "
      "precision=%R, scale=%R, null_ok=%R) at %p>",
      Py_TYPE(column)->tp_name, column->name, column->type_code,
      column->display_size, column->internal_size, column->precision,
      column->scale, column->null_ok, column);
}

int column_init(ColumnObject *column, PyObject *args, PyObject *kwargs) {
  PyObject *name;
  static char *kwlist[] = {"", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O", kwlist, &name)) {
    return -1;
  }

  if (!PyUnicode_Check(name)) {
    PyErr_SetString(PyExc_TypeError, "__init__ argument 1 must be a string");
    return -1;
  }

  PyObject *tmp_name = column->name;
  Py_INCREF(name);
  column->name = name;
  Py_XDECREF(tmp_name);

  PyObject *tmp_type_code = column->type_code;
  Py_INCREF(Py_None);
  column->type_code = Py_None;
  Py_XDECREF(tmp_type_code);

  PyObject *tmp_display_size = column->display_size;
  Py_INCREF(Py_None);
  column->display_size = Py_None;
  Py_XDECREF(tmp_display_size);

  PyObject *tmp_internal_size = column->internal_size;
  Py_INCREF(Py_None);
  column->internal_size = Py_None;
  Py_XDECREF(tmp_internal_size);

  PyObject *tmp_precision = column->precision;
  Py_INCREF(Py_None);
  column->precision = Py_None;
  Py_XDECREF(tmp_precision);

  PyObject *tmp_scale = column->scale;
  Py_INCREF(Py_None);
  column->scale = Py_None;
  Py_XDECREF(tmp_scale);

  PyObject *tmp_null_ok = column->null_ok;
  Py_INCREF(Py_None);
  column->null_ok = Py_None;
  Py_XDECREF(tmp_null_ok);

  return 0;
}

PyDoc_STRVAR(ColumnType_name_doc, "name of the returned column");
PyDoc_STRVAR(
    ColumnType_unsupported_doc,
    "always set to ``None`` (required by DB-API 2.0 spec, but not supported)");

static PyMemberDef column_members[] = {
    {"name", T_OBJECT, offsetof(ColumnObject, name), READONLY,
     ColumnType_name_doc},
    {"type_code", T_OBJECT, offsetof(ColumnObject, type_code), READONLY,
     ColumnType_unsupported_doc},
    {"display_size", T_OBJECT, offsetof(ColumnObject, display_size), READONLY,
     ColumnType_unsupported_doc},
    {"internal_size", T_OBJECT, offsetof(ColumnObject, internal_size), READONLY,
     ColumnType_unsupported_doc},
    {"precision", T_OBJECT, offsetof(ColumnObject, precision), READONLY,
     ColumnType_unsupported_doc},
    {"scale", T_OBJECT, offsetof(ColumnObject, scale), READONLY,
     ColumnType_unsupported_doc},
    {"null_ok", T_OBJECT, offsetof(ColumnObject, null_ok), READONLY,
     ColumnType_unsupported_doc},
    {NULL, 0, 0, 0, NULL}};

PyDoc_STRVAR(ColumnType_doc, "Description of a column returned by the query.");

// clang-format off
PyTypeObject ColumnType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "mgclient.Column",
    .tp_basicsize = sizeof(ColumnObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)column_dealloc,
    .tp_repr = (reprfunc)column_repr,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = ColumnType_doc,
    .tp_members = column_members,
    .tp_init = (initproc)column_init,
    .tp_new = PyType_GenericNew
};
// clang-format on
