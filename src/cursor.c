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

#include "cursor.h"

#include <structmember.h>

#include "column.h"
#include "connection.h"
#include "exceptions.h"

static void cursor_dealloc(CursorObject *cursor) {
  Py_CLEAR(cursor->conn);
  Py_CLEAR(cursor->rows);
  Py_CLEAR(cursor->description);
  Py_TYPE(cursor)->tp_free(cursor);
}

int cursor_init(CursorObject *cursor, PyObject *args, PyObject *kwargs) {
  ConnectionObject *conn = NULL;

  static char *kwlist[] = {"", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O", kwlist, &conn)) {
    return -1;
  }

  if (Py_TYPE(conn) != &ConnectionType) {
    PyErr_Format(PyExc_TypeError, "__init__ argument 1 must be of type '%s'",
                 ConnectionType.tp_name);
    return -1;
  }

  Py_INCREF(conn);
  cursor->conn = conn;

  cursor->status = CURSOR_STATUS_READY;
  cursor->hasresults = 0;
  cursor->arraysize = 1;
  cursor->rows = NULL;
  cursor->description = NULL;
  return 0;
}

PyObject *cursor_new(PyTypeObject *subtype, PyObject *args, PyObject *kwargs) {
  // Unused args.
  (void)args;
  (void)kwargs;

  PyObject *cursor = subtype->tp_alloc(subtype, 0);
  if (!cursor) {
    return NULL;
  }
  ((CursorObject *)cursor)->status = CURSOR_STATUS_CLOSED;
  return cursor;
}

// Reset the cursor into state where it can be used for executing a new query,
// but caling fetch* throws an exception.
static void cursor_reset(CursorObject *cursor) {
  Py_CLEAR(cursor->rows);
  Py_CLEAR(cursor->description);
  cursor->hasresults = 0;
  cursor->rowcount = -1;
  cursor->status = CURSOR_STATUS_READY;
}

// clang-format off
PyDoc_STRVAR(cursor_close_doc,
"close()\n\
--\n\
\n\
Close the cursor now.\n\
\n\
The cursor will be unusable from this point forward; an :exc:`InterfaceError`\n\
will be raised if any operation is attempted with the cursor.");
// clang-format on

PyObject *cursor_close(CursorObject *cursor, PyObject *args) {
  // Unused args;
  (void)args;

  assert(!args);
  if (cursor->status == CURSOR_STATUS_EXECUTING) {
    assert(cursor->conn->status == CONN_STATUS_EXECUTING);
    assert(cursor->conn->lazy);

    // Cannot close cursor while executing a query because query execution might
    // raise an error.
    PyErr_SetString(InterfaceError,
                    "cannot close cursor during execution of a query");
    return NULL;
  }

  Py_CLEAR(cursor->conn);
  cursor_reset(cursor);
  cursor->status = CURSOR_STATUS_CLOSED;

  Py_RETURN_NONE;
}

static int cursor_set_description(CursorObject *cursor, PyObject *columns) {
  assert(PyList_Check(columns));
  assert(cursor->description == NULL);
  if (!columns) {
    return 0;
  }
  PyObject *description = NULL;
  if (!(description = PyList_New(PyList_Size(columns)))) {
    goto failure;
  }
  for (Py_ssize_t i = 0; i < PyList_Size(columns); ++i) {
    PyObject *entry = PyObject_CallFunctionObjArgs(
        (PyObject *)&ColumnType, PyList_GetItem(columns, i), NULL);
    if (!entry) {
      goto failure;
    }
    PyList_SET_ITEM(description, i, entry);
  }

  cursor->description = description;
  return 0;

failure:
  if (PyErr_WarnEx(Warning, "failed to obtain result column names", 2) < 0) {
    return -1;
  }
  Py_XDECREF(description);
  return 0;
}

// clang-format off
PyDoc_STRVAR(cursor_execute_doc,
"execute(query, params=None)\n\
--\n\
\n\
Execute a database operation.\n\
\n\
Parameters may be provided as a mapping and will be bound to variables in\n\
the operation. Variables are specified with named (``$name``)\n\
placeholders.\n\
\n\
This method always returns ``None``.\n");
// clang-format on

PyObject *cursor_execute(CursorObject *cursor, PyObject *args) {
  const char *query = NULL;
  PyObject *pyparams = NULL;
  if (!PyArg_ParseTuple(args, "s|O", &query, &pyparams)) {
    return NULL;
  }

  if (cursor->status == CURSOR_STATUS_CLOSED) {
    PyErr_SetString(InterfaceError, "cursor closed");
    return NULL;
  }

  if (connection_raise_if_bad_status(cursor->conn) < 0) {
    return NULL;
  }

  if (cursor->conn->status == CONN_STATUS_EXECUTING) {
    assert(cursor->conn->lazy);
    PyErr_SetString(InterfaceError,
                    "cannot call execute during execution of a query");
    return NULL;
  }

  assert(cursor->status == CURSOR_STATUS_READY);

  cursor_reset(cursor);

  if (!cursor->conn->autocommit && cursor->conn->status == CONN_STATUS_READY) {
    if (connection_begin(cursor->conn) < 0) {
      goto cleanup;
    }
  }

  PyObject *columns;
  if (connection_run(cursor->conn, query, pyparams, &columns) < 0) {
    goto cleanup;
  }

  if (cursor_set_description(cursor, columns) < 0) {
    Py_XDECREF(columns);
    goto cleanup;
  }
  Py_XDECREF(columns);

  // In lazy mode, results are pulled when fetch is called.
  if (cursor->conn->lazy) {
    cursor->status = CURSOR_STATUS_EXECUTING;
    cursor->hasresults = 1;
    cursor->rowcount = -1;
    Py_RETURN_NONE;
  }

  // Pull all results now.
  if (!(cursor->rows = PyList_New(0))) {
    goto discard_all;
  }

  int status;
  status = connection_pull(cursor->conn, 0);  // PULL_ALL
  if (status != 0) {
    goto cleanup;
  }

  PyObject *row;
  while ((status = connection_fetch(cursor->conn, &row, NULL)) == 1) {
    if (PyList_Append(cursor->rows, row) < 0) {
      Py_DECREF(row);
      goto discard_all;
    }
    Py_DECREF(row);
  }
  if (status < 0) {
    connection_handle_error(cursor->conn, status);
    goto cleanup;
  }

  cursor->hasresults = 1;
  cursor->rowindex = 0;
  cursor->rowcount = PyList_Size(cursor->rows);
  Py_RETURN_NONE;

discard_all:
  connection_discard_all(cursor->conn);

cleanup:
  cursor_reset(cursor);
  return NULL;
}

// clang-format off
PyDoc_STRVAR(cursor_fetchone_doc,
"fetchone()\n\
--\n\
\n\
Fetch the next row of query results, returning a single tuple, or ``None``\n\
when no more data is available.\n\
\n\
An :exc:`InterfaceError` is raised if the previous call to :meth:`.execute()`\n\
did not produce any results or no call was issued yet.");
// clang-format on

PyObject *cursor_fetchone(CursorObject *cursor, PyObject *args) {
  // Unused args.
  (void)args;

  assert(!args);

  if (!cursor->hasresults) {
    PyErr_SetString(InterfaceError, "no results available");
    return NULL;
  }

  if (cursor->conn->lazy) {
    if (cursor->status == CURSOR_STATUS_READY) {
      // All rows are pulled so we have to return None.
      Py_RETURN_NONE;
    }

    if (cursor->status == CURSOR_STATUS_EXECUTING) {
      int pull_status = connection_pull(cursor->conn, 1);
      if (pull_status != 0) {
        cursor_reset(cursor);
        return NULL;
      }
    }

    PyObject *row = NULL;
    // fetchone returns an exact result, this method can't be called twice for
    // a single pull call. If called twice for one pull call the second call
    // has to return something which is not correct form the cursor interface
    // point of view.
    //
    // The problem is also if the second fetch call ends up with a database
    // error, from the user perspective that will look like an error related to
    // the first pull.
    int has_more_first = 0;
    int has_more_second = 0;
    int fetch_status_first =
        connection_fetch(cursor->conn, &row, &has_more_first);
    int fetch_status_second =
        connection_fetch(cursor->conn, NULL, &has_more_second);
    if (fetch_status_first == -1 || fetch_status_second == -1) {
      if (row) {
        Py_DECREF(row);
      }
      connection_handle_error(cursor->conn, -1);
      cursor_reset(cursor);
      return NULL;
    } else if (fetch_status_first == 0) {
      if (row) {
        Py_DECREF(row);
      }
      if (has_more_first) {
        cursor->status = CURSOR_STATUS_EXECUTING;
      } else {
        cursor->status = CURSOR_STATUS_READY;
      }
      Py_RETURN_NONE;
    } else if (fetch_status_first == 1) {
      if (has_more_second) {
        cursor->status = CURSOR_STATUS_EXECUTING;
      } else {
        cursor->status = CURSOR_STATUS_READY;
      }
      return row;
    } else {
      // This should never happen, if it happens, some case is not covered.
      assert(0);
    }
  }

  assert(cursor->rowcount >= 0);
  if (cursor->rowindex < cursor->rowcount) {
    PyObject *row = PyList_GET_ITEM(cursor->rows, cursor->rowindex++);
    Py_INCREF(row);
    return row;
  }

  Py_RETURN_NONE;
}

// clang-format off
PyDoc_STRVAR(
cursor_fetchmany_doc,
"fetchmany(size=None)\n\
--\n\
\n\
Fetch the next set of rows of query results, returning a list of tuples.\n\
An empty list is returned when no more data is available.\n\
\n\
The number of rows to fetch per call is specified by the parameter. If it\n\
is not given the cursor's :attr:`arraysize` determines the number of rows\n\
to be fetched. Fewer rows may be returned in case there is less rows available\n\
than requested.\n\
\n\
An :exc:`InterfaceError` is raised if the previous call to :meth:`.execute()`\n\
did not produce any results or no call was issued yet.");
// clang-format on

PyObject *cursor_fetchmany(CursorObject *cursor, PyObject *args,
                           PyObject *kwargs) {
  // TODO(gitbuda): Implement fetchmany by pulling the exact number of records.
  static char *kwlist[] = {"size", NULL};
  PyObject *pysize = NULL;
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O", kwlist, &pysize)) {
    return NULL;
  }

  if (!cursor->hasresults) {
    PyErr_SetString(InterfaceError, "no results available");
    return NULL;
  }

  long size = cursor->arraysize;

  if (pysize && pysize != Py_None) {
    size = PyLong_AsLong(pysize);
    if (PyErr_Occurred()) {
      return NULL;
    }
  }

  if (cursor->conn->lazy) {
    PyObject *results;
    if (!(results = PyList_New(0))) {
      return NULL;
    }
    for (long i = 0; i < size; ++i) {
      PyObject *row;
      if (!(row = cursor_fetchone(cursor, NULL))) {
        Py_DECREF(results);
        return NULL;
      }
      if (row == Py_None) {
        break;
      }
      if (PyList_Append(results, row) < 0) {
        Py_DECREF(row);
        Py_DECREF(results);
        connection_discard_all(cursor->conn);
        cursor_reset(cursor);
        return NULL;
      }
    }
    return results;
  }

  assert(cursor->rowcount >= 0);

  PyObject *rows;
  Py_ssize_t new_rowindex = cursor->rowindex + size;
  new_rowindex =
      new_rowindex < cursor->rowcount ? new_rowindex : cursor->rowcount;
  if (!(rows = PyList_GetSlice(cursor->rows, cursor->rowindex, new_rowindex))) {
    return NULL;
  }
  cursor->rowindex = new_rowindex;
  return rows;
}

// clang-format off
PyDoc_STRVAR(cursor_fetchall_doc,
"fetchall()\n\
--\n\
\n\
Fetch all (remaining) rows of query results, returning them as a list of\n\
tuples.\n\
\n\
An :exc:`InterfaceError` is raised if the previous call to :meth:`.execute()`\n\
did not produce any results or no call was issued yet.");
// clang-format on

PyObject *cursor_fetchall(CursorObject *cursor, PyObject *args) {
  // Unused args.
  (void)args;

  assert(!args);

  if (!cursor->hasresults) {
    PyErr_SetString(InterfaceError, "no results available");
    return NULL;
  }

  if (cursor->conn->lazy) {
    PyObject *results;
    if (!(results = PyList_New(0))) {
      return NULL;
    }

    if (cursor->status == CURSOR_STATUS_READY) {
      return results;
    }

    if (cursor->status == CURSOR_STATUS_EXECUTING) {
      int pull_status = 0;
      pull_status = connection_pull(cursor->conn, 0);
      if (pull_status != 0) {
        Py_DECREF(results);
        connection_handle_error(cursor->conn, pull_status);
        cursor_reset(cursor);
        return NULL;
      }
    }

    while (1) {
      PyObject *row = NULL;
      int fetch_status = connection_fetch(cursor->conn, &row, NULL);
      if (fetch_status == 0) {
        cursor->status = CURSOR_STATUS_READY;
        break;
      } else if (fetch_status == 1) {
        if (PyList_Append(results, row) < 0) {
          Py_DECREF(row);
          Py_DECREF(results);
          connection_discard_all(cursor->conn);
          cursor_reset(cursor);
          return NULL;
        }
      } else {
        Py_DECREF(results);
        connection_handle_error(cursor->conn, fetch_status);
        cursor_reset(cursor);
        return NULL;
      }
      if (!row) {
        Py_DECREF(results);
        return NULL;
      }
    }

    return results;
  }

  assert(cursor->rowcount >= 0);

  PyObject *rows;
  if (!(rows = PyList_GetSlice(cursor->rows, cursor->rowindex,
                               cursor->rowcount))) {
    return NULL;
  }
  cursor->rowindex = cursor->rowcount;
  return rows;
}

PyDoc_STRVAR(
    cursor_setinputsizes_doc,
    "This method does nothing, but it is required by the DB-API 2.0 spec.");

PyObject *cursor_setinputsizes(CursorObject *cursor, PyObject *args) {
  PyObject *sizes;
  if (!PyArg_ParseTuple(args, "O", &sizes)) {
    return NULL;
  }
  if (cursor->status == CURSOR_STATUS_CLOSED) {
    PyErr_SetString(InterfaceError, "cursor closed");
    return NULL;
  }
  Py_RETURN_NONE;
}

PyDoc_STRVAR(
    cursor_setoutputsizes_doc,
    "This method does nothing, but it is required by the DB-API 2.0 spec.");

PyObject *cursor_setoutputsizes(CursorObject *cursor, PyObject *args) {
  long size;
  long column;
  if (!PyArg_ParseTuple(args, "l|l", &size, &column)) {
    return NULL;
  }
  if (cursor->status == CURSOR_STATUS_CLOSED) {
    PyErr_SetString(InterfaceError, "cursor closed");
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyMethodDef cursor_methods[] = {
    {"close", (PyCFunction)cursor_close, METH_NOARGS, cursor_close_doc},
    {"execute", (PyCFunction)cursor_execute, METH_VARARGS, cursor_execute_doc},
    {"fetchone", (PyCFunction)cursor_fetchone, METH_NOARGS,
     cursor_fetchone_doc},
    {"fetchmany", (PyCFunction)cursor_fetchmany, METH_VARARGS | METH_KEYWORDS,
     cursor_fetchmany_doc},
    {"fetchall", (PyCFunction)cursor_fetchall, METH_NOARGS,
     cursor_fetchall_doc},
    {"setinputsizes", (PyCFunction)cursor_setinputsizes, METH_VARARGS,
     cursor_setinputsizes_doc},
    {"setoutputsizes", (PyCFunction)cursor_setoutputsizes, METH_VARARGS,
     cursor_setoutputsizes_doc},
    {NULL, NULL, 0, NULL}};

// clang-format off
PyDoc_STRVAR(CursorType_rowcount_doc,
"This read-only attribute specifies the number of rows that the last\n\
:meth:`.execute()` produced.\n\
\n\
The attribute is -1 in case no :meth:`.execute()` has been performed or\n\
the rowcount of the last operation cannot be determined by the interface.");

PyDoc_STRVAR(CursorType_arraysize_doc,
"This read/write attribute specifies the number of rows to fetch at a time\n\
with :meth:`.fetchmany()`. It defaults to 1 meaning to fetch a single row at\n\
a time.");

PyDoc_STRVAR(CursorType_description_doc,
"This read-only attribute is a list of :class:`Column` objects.\n\
\n\
Each of those object has attributed describing one result column:\n\
\n\
 - :attr:`.name`\n\
 - :attr:`.type_code`\n\
 - :attr:`.display_size`\n\
 - :attr:`.internal_size`\n\
 - :attr:`.precision`\n\
 - :attr:`.scale`\n\
 - :attr:`.null_ok`\n\
\n\
Only the name attribute is set to the name of column returned by the\n\
database. The rest are always set to ``None`` and are only here for\n\
compatibility with DB-API 2.0.\n\
\n\
This attribute will be ``None`` for operations that do not return rows\n\
or if the cursor has not had an operation invoked via the :meth:`.execute()`\n\
method yet.");
// clang-format on

static PyMemberDef cursor_members[] = {
    {"rowcount", T_PYSSIZET, offsetof(CursorObject, rowcount), READONLY,
     CursorType_rowcount_doc},
    {"arraysize", T_LONG, offsetof(CursorObject, arraysize), 0,
     CursorType_arraysize_doc},
    {"description", T_OBJECT, offsetof(CursorObject, description), READONLY,
     CursorType_description_doc},
    {NULL, 0, 0, 0, NULL}};

// clang-format off
PyDoc_STRVAR(cursor_doc,
"Allows execution of database commands.\n\
\n\
Cursors are created by the :meth:`Connection.cursor()` method and they are\n\
bound to the connection for the entire lifetime. Cursors created by the same\n\
connection are not isolated, any changes done to the database by one cursor\n\
are immediately visible by the other cursors.\n\
\n\
Cursor objects are not thread-safe.");
// clang-format on

// clang-format off
PyTypeObject CursorType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "mgclient.Cursor",
    .tp_basicsize = sizeof(CursorObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)cursor_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = cursor_doc,
    .tp_methods = cursor_methods,
    .tp_members = cursor_members,
    .tp_init = (initproc)cursor_init,
    .tp_new = (newfunc)cursor_new
};
// clang-format on
