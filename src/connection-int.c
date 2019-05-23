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

#include "connection.h"

#include "exceptions.h"
#include "glue.h"

int connection_raise_if_bad_status(const ConnectionObject *conn) {
  if (conn->status == CONN_STATUS_BAD) {
    PyErr_SetString(InterfaceError, "bad session");
    return -1;
  }
  if (conn->status == CONN_STATUS_CLOSED) {
    PyErr_SetString(InterfaceError, "session closed");
    return -1;
  }
  return 0;
}

void connection_handle_error(ConnectionObject *conn) {
  if (mg_session_status(conn->session) == MG_SESSION_BAD) {
    conn->status = CONN_STATUS_BAD;
  }
  PyErr_SetString(DatabaseError, mg_session_error(conn->session));
}

int connection_run_without_results(ConnectionObject *conn, const char *query) {
  int status = mg_session_run(conn->session, query, NULL, NULL);
  if (status != 0) {
    connection_handle_error(conn);
    return -1;
  }

  while (1) {
    mg_result *result;
    int status = mg_session_pull(conn->session, &result);
    if (status == 0) {
      break;
    }
    if (status == 1) {
      if (PyErr_WarnFormat(Warning, 2,
                           "unexpected rows received after executing '%s'",
                           query) < 0) {
        return -1;
      }
    }
    if (status < 0) {
      connection_handle_error(conn);
      return -1;
    }
  }

  return 0;
}

int connection_run(ConnectionObject *conn, const char *query, PyObject *params,
                   PyObject **columns) {
  // This should be used to start the execution of a query, so we validate we're
  // in a valid state for query execution.
  assert((conn->autocommit && conn->status == CONN_STATUS_READY) ||
         (!conn->autocommit && conn->status == CONN_STATUS_IN_TRANSACTION));

  mg_map *mg_params = NULL;
  if (params) {
    mg_params = py_dict_to_mg_map(params);
    if (!mg_params) {
      return -1;
    }
  }

  const mg_list *mg_columns;
  int status = mg_session_run(conn->session, query, mg_params, &mg_columns);
  mg_map_destroy(mg_params);

  if (status != 0) {
    connection_handle_error(conn);
    return -1;
  }

  if (columns) {
    *columns = mg_list_to_py_list(mg_columns);
  }

  conn->status = CONN_STATUS_EXECUTING;
  return 0;
}

int connection_pull(ConnectionObject *conn, PyObject **row) {
  // This should be used to pull results during query execution, so we validate
  // that there is a running query.
  assert(conn->status == CONN_STATUS_EXECUTING);

  mg_result *result;
  int status = mg_session_pull(conn->session, &result);

  if (status <= 0) {
    conn->status =
        conn->autocommit ? CONN_STATUS_READY : CONN_STATUS_IN_TRANSACTION;
  }

  if (status < 0) {
    connection_handle_error(conn);
    return -1;
  }

  if (status == 0) {
    return 0;
  }

  if (row) {
    PyObject *pyresult = mg_list_to_py_tuple(mg_result_row(result));
    if (!pyresult) {
      connection_discard_all(conn);
      return -1;
    }
    *row = pyresult;
  }
  return 1;
}

int connection_begin(ConnectionObject *conn) {
  assert(!conn->lazy && conn->status == CONN_STATUS_READY);

  // send BEGIN command and expect no results
  if (connection_run_without_results(conn, "BEGIN") < 0) {
    return -1;
  }

  conn->status = CONN_STATUS_IN_TRANSACTION;
  return 0;
}

void connection_discard_all(ConnectionObject *conn) {
  assert(conn->status == CONN_STATUS_EXECUTING);
  assert(PyErr_Occurred());

  PyObject *prev_exc;
  {
    PyObject *type, *traceback;
    PyErr_Fetch(&type, &prev_exc, &traceback);
    PyErr_NormalizeException(&type, &prev_exc, &traceback);
    Py_XDECREF(type);
    Py_XDECREF(traceback);
  }

  int status;
  mg_result *result;
  while ((status = mg_session_pull(conn->session, &result)) == 1)
    ;

  if (status == 0) {
    // We successfuly discarded all of the results.
    PyErr_SetString(InterfaceError,
                    "There was an error fetching query results. The query has "
                    "executed successfully but the results were discarded.");
    PyObject *type, *curr_exc, *traceback;
    PyErr_Fetch(&type, &curr_exc, &traceback);
    PyErr_NormalizeException(&type, &curr_exc, &traceback);
    PyException_SetCause(curr_exc, prev_exc);
    PyErr_Restore(type, curr_exc, traceback);
  } else {
    // There was a database error while pulling the rest of the results.
    connection_handle_error(conn);
    PyObject *pulling_exc;
    {
      PyObject *type, *traceback;
      PyErr_Fetch(&type, &pulling_exc, &traceback);
      PyErr_NormalizeException(&type, &pulling_exc, &traceback);
      Py_XDECREF(type);
      Py_XDECREF(traceback);
    }

    PyErr_SetString(
        InterfaceError,
        "There was an error fetching query results. While pulling the rest of "
        "the results from server to discard them, another exception occurred. "
        "It is not certain whether the query executed successfuly.");
    PyObject *type, *curr_exc, *traceback;
    PyErr_Fetch(&type, &curr_exc, &traceback);
    PyErr_NormalizeException(&type, &curr_exc, &traceback);
    PyException_SetCause(pulling_exc, prev_exc);
    PyException_SetCause(curr_exc, pulling_exc);
    PyErr_Restore(type, curr_exc, traceback);
  }

  if (conn->status != CONN_STATUS_BAD) {
    conn->status =
        conn->autocommit ? CONN_STATUS_READY : CONN_STATUS_IN_TRANSACTION;
  }
}
