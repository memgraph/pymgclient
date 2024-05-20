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

#include "connection.h"

#include <structmember.h>

#include "cursor.h"
#include "exceptions.h"

static void connection_dealloc(ConnectionObject *conn) {
  mg_session_destroy(conn->session);
  Py_TYPE(conn)->tp_free(conn);
}

static int execute_trust_callback(const char *hostname, const char *ip_address,
                                  const char *key_type, const char *fingerprint,
                                  PyObject *pycallback) {
  PyObject *result = PyObject_CallFunction(pycallback, "ssss", hostname,
                                           ip_address, key_type, fingerprint);
  if (!result) {
    return -1;
  }
  int status = PyObject_IsTrue(result);
  Py_DECREF(result);
  return !status;
}

static int connection_init(ConnectionObject *conn, PyObject *args,
                           PyObject *kwargs) {
  static char *kwlist[] = {
      "host",     "address",        "port",        "scheme",  "username",
      "password", "credentials",    "client_name", "sslmode", "sslcert",
      "sslkey",   "trust_callback", "lazy",        NULL};

  const char *host = NULL;
  const char *address = NULL;
  int port = -1;
  const char *scheme = NULL;
  const char *username = NULL;
  const char *password = NULL;
  const char *credentials = NULL;
  const char *client_name = NULL;
  int sslmode_int = MG_SSLMODE_DISABLE;
  const char *sslcert = NULL;
  const char *sslkey = NULL;
  PyObject *trust_callback = NULL;
  int lazy = 0;

  if (!PyArg_ParseTupleAndKeywords(
          args, kwargs, "|$ssisssssissOp", kwlist, &host, &address, &port,
          &scheme, &username, &password, &credentials, &client_name,
          &sslmode_int, &sslcert, &sslkey, &trust_callback, &lazy)) {
    return -1;
  }

  if (port < 0 || port > 65535) {
    PyErr_SetString(PyExc_ValueError, "port out of range");
    return -1;
  }

  enum mg_sslmode sslmode;
  switch (sslmode_int) {
    case MG_SSLMODE_DISABLE:
    case MG_SSLMODE_REQUIRE:
      sslmode = sslmode_int;
      break;
    default:
      PyErr_SetString(PyExc_ValueError, "invalid sslmode");
      return -1;
  }

  if (trust_callback && !PyCallable_Check(trust_callback)) {
    PyErr_SetString(PyExc_TypeError,
                    "trust_callback argument must be callable");
    return -1;
  }

  mg_session_params *params = mg_session_params_make();
  if (!params) {
    PyErr_SetString(PyExc_RuntimeError,
                    "couldn't allocate session parameters object");
    return -1;
  }
  mg_session_params_set_host(params, host);
  mg_session_params_set_port(params, (uint16_t)port);
  mg_session_params_set_address(params, address);
  mg_session_params_set_scheme(params, scheme);
  mg_session_params_set_username(params, username);
  mg_session_params_set_password(params, password);
  mg_session_params_set_credentials(params, credentials);
  if (client_name) {
    mg_session_params_set_user_agent(params, client_name);
  }
  mg_session_params_set_sslmode(params, sslmode);
  mg_session_params_set_sslcert(params, sslcert);
  mg_session_params_set_sslkey(params, sslkey);
  if (trust_callback) {
    mg_session_params_set_trust_callback(
        params, (mg_trust_callback_type)execute_trust_callback);
    mg_session_params_set_trust_data(params, (void *)trust_callback);
  }

  mg_session *session;
  {
    int status = mg_connect(params, &session);
    mg_session_params_destroy(params);
    if (status != 0) {
      // TODO(mtomic): maybe convert MG_ERROR_* codes to different kinds of
      // Python exceptions
      PyErr_SetString(OperationalError, mg_session_error(session));
      mg_session_destroy(session);
      return -1;
    }
  }

  conn->session = session;
  conn->status = CONN_STATUS_READY;
  conn->lazy = 0;
  conn->autocommit = 0;

  if (lazy) {
    conn->lazy = 1;
    conn->autocommit = 1;
  }

  return 0;
}

static PyObject *connection_new(PyTypeObject *subtype, PyObject *args,
                                PyObject *kwargs) {
  // Unused args.
  (void)args;
  (void)kwargs;

  PyObject *conn = subtype->tp_alloc(subtype, 0);
  if (!conn) {
    return NULL;
  }
  ((ConnectionObject *)conn)->status = CONN_STATUS_BAD;
  return conn;
}

// clang-format off
PyDoc_STRVAR(connection_close_doc,
"close()\n\
--\n\
\n\
Close the connection now.\n\
\n\
The connection will be unusable from this point forward; an :exc:`InterfaceError`\n\
will be raised if any operation is attempted with the connection. The same applies\n\
to all :class:`.Cursor` objects trying to use the connection.\n\
\n\
Note that closing a connection without committing the changes will cause an implicit\n\
rollback to be performed.");
// clang-format on

static PyObject *connection_close(ConnectionObject *conn, PyObject *args) {
  // Unused args.
  (void)args;

  assert(!args);

  if (conn->status == CONN_STATUS_EXECUTING) {
    // This can only happen if connection is in lazy execution mode.
    assert(conn->lazy);
    PyErr_SetString(InterfaceError,
                    "cannot close connection during execution of a query");
    return NULL;
  }

  // No need to rollback, closing the connection will automatically
  // rollback any open transactions.
  mg_session_destroy(conn->session);
  conn->session = NULL;
  conn->status = CONN_STATUS_CLOSED;

  Py_RETURN_NONE;
}

// clang-format off
PyDoc_STRVAR(connection_commit_doc,
"commit()\n\
--\n\
\n\
Commit any pending transaction to the database.\n\
\n\
If auto-commit is turned on, this method does nothing.");
// clang-format on

static PyObject *connection_commit(ConnectionObject *conn, PyObject *args) {
  // Unused args.
  (void)args;

  assert(!args);

  if (connection_raise_if_bad_status(conn) < 0) {
    return NULL;
  }

  if (conn->status == CONN_STATUS_EXECUTING) {
    // This can only happen if connection is in lazy execution mode. In
    // that case, autocommit must be enabled and this method does nothing.
    assert(conn->lazy && conn->autocommit);
    Py_RETURN_NONE;
  }

  if (conn->autocommit || conn->status == CONN_STATUS_READY) {
    Py_RETURN_NONE;
  }

  assert(conn->status == CONN_STATUS_IN_TRANSACTION);

  // send COMMIT command and expect no results
  if (connection_run_without_results(conn, "COMMIT") < 0) {
    return NULL;
  }

  conn->status = CONN_STATUS_READY;
  Py_RETURN_NONE;
}

// clang-format off
PyDoc_STRVAR(connection_rollback_doc,
"rollback()\n\
--\n\
\n\
Roll back to the start of any pending transaction.\n\
\n\
If auto-commit is turned on, this method does nothing.");
// clang-format on

static PyObject *connection_rollback(ConnectionObject *conn, PyObject *args) {
  // Unused args.
  (void)args;

  assert(!args);
  if (connection_raise_if_bad_status(conn) < 0) {
    return NULL;
  }

  if (conn->status == CONN_STATUS_EXECUTING) {
    // This can only happen if connection is in lazy execution mode. In
    // that case, autocommit must be enabled and this method does nothing.
    assert(conn->lazy && conn->autocommit);
    Py_RETURN_NONE;
  }

  if (conn->autocommit || conn->status == CONN_STATUS_READY) {
    Py_RETURN_NONE;
  }

  assert(conn->status == CONN_STATUS_IN_TRANSACTION);

  // send ROLLBACK command and expect no results
  if (connection_run_without_results(conn, "ROLLBACK") < 0) {
    return NULL;
  }

  conn->status = CONN_STATUS_READY;
  Py_RETURN_NONE;
}

// clang-format off
PyDoc_STRVAR(connection_cursor_doc,
"cursor()\n\
--\n\
\n\
Return a new :class:`Cursor` object using the connection.");
// clang-format on

static PyObject *connection_cursor(ConnectionObject *conn, PyObject *args) {
  // Unused args.
  (void)args;

  assert(!args);

  if (connection_raise_if_bad_status(conn) < 0) {
    return NULL;
  }

  return PyObject_CallFunctionObjArgs((PyObject *)&CursorType, conn, NULL);
}

// clang-format off
PyDoc_STRVAR(
ConnectionType_autocommit_doc,
"This read/write attribute specifies whether executed statements\n\
have immediate effect in the database.\n\
\n\
If ``True``, every executed statement has immediate effect.\n\
\n\
If ``False``, a new transaction is started at the execution of the first\n\
command. Transactions must be manually terminated using :meth:`commit` or\n\
:meth:`rollback` methods.");
// clang-format on

PyObject *connection_autocommit_get(ConnectionObject *conn, void *data) {
  (void)data;
  if (conn->autocommit) {
    Py_RETURN_TRUE;
  } else {
    Py_RETURN_FALSE;
  }
}

int connection_autocommit_set(ConnectionObject *conn, PyObject *value,
                              void *data) {
  (void)data;
  if (!value) {
    PyErr_SetString(InterfaceError, "cannot delete autocommit property");
    return -1;
  }
  if (conn->lazy) {
    PyErr_SetString(InterfaceError,
                    "autocommit is always enabled in lazy mode");
    return -1;
  }
  if (conn->status == CONN_STATUS_EXECUTING ||
      conn->status == CONN_STATUS_IN_TRANSACTION) {
    PyErr_SetString(InterfaceError,
                    "cannot change autocommit property while in a transaction");
    return -1;
  }
  int tf = PyObject_IsTrue(value);
  if (tf < 0) {
    return -1;
  }
  conn->autocommit = tf ? 1 : 0;

  return 0;
}

static PyMethodDef connection_methods[] = {
    {"close", (PyCFunction)connection_close, METH_NOARGS, connection_close_doc},
    {"commit", (PyCFunction)connection_commit, METH_NOARGS,
     connection_commit_doc},
    {"rollback", (PyCFunction)connection_rollback, METH_NOARGS,
     connection_rollback_doc},
    {"cursor", (PyCFunction)connection_cursor, METH_NOARGS,
     connection_cursor_doc},
    {NULL, NULL, 0, NULL}};

// clang-format off
PyDoc_STRVAR(ConnectionType_status_doc,
"Status of the connection.\n\
\n\
It's value can be one of the following macros:\n\
   * :data:`mgclient.CONN_STATUS_READY`\n\
        The connection is currently not in a transaction and\n\
        it is ready to start executing the next command.\n\
\n\
   * :data:`mgclient.CONN_STATUS_BAD`\n\
        Something went wrong with the connection, it cannot be\n\
        used for command execution anymore.\n\
\n\
   * :data:`mgclient.CONN_STATUS_CLOSED`\n\
        The connection was closed by user, it cannot be\n\
        used for command execution anymore.\n\
\n\
   * :data:`mgclient.CONN_STATUS_IN_TRANSACTION`\n\
        The connection is currently in an implicitly started\n\
        transaction.\n\
\n\
   * :data:`mgclient.CONN_STATUS_EXECUTING`\n\
        The connection is currently executing a query. This status\n\
        can only be seen for lazy connections.");
// clang-format on

static PyMemberDef connection_members[] = {
    {"status", T_INT, offsetof(ConnectionObject, status), READONLY,
     ConnectionType_status_doc},
    {NULL}};

static PyGetSetDef connection_getset[] = {
    {"autocommit", (getter)connection_autocommit_get,
     (setter)connection_autocommit_set, ConnectionType_autocommit_doc, NULL},
    {NULL}};

// clang-format off
PyDoc_STRVAR(ConnectionType_doc,
"Encapsulates a database connection.\n\
\n\
New instances are created using the factory function :func:`connect`.\n\
\n\
Connections are not thread-safe.");
// clang-format on

// clang-format off
PyTypeObject ConnectionType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "mgclient.Connection",
    .tp_doc = ConnectionType_doc,
    .tp_basicsize = sizeof(ConnectionObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_dealloc = (destructor)connection_dealloc,
    .tp_methods = connection_methods,
    .tp_members = connection_members,
    .tp_getset = connection_getset,
    .tp_init = (initproc)connection_init,
    .tp_new = (newfunc)connection_new};
// clang-format on
