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

#ifndef PYMGCLIENT_CONNECTION_H
#define PYMGCLIENT_CONNECTION_H

#include <Python.h>

#include <mgclient.h>

// Connection status constants.
#define CONN_STATUS_READY 0
#define CONN_STATUS_IN_TRANSACTION 1
#define CONN_STATUS_EXECUTING 2
#define CONN_STATUS_CLOSED 3
#define CONN_STATUS_BAD (-1)

// clang-format off
typedef struct ConnectionObject {
  PyObject_HEAD

  mg_session *session;
  int status;
  int autocommit;
  int lazy;
} ConnectionObject;
// clang-format on

extern PyTypeObject ConnectionType;

int connection_raise_if_bad_status(const ConnectionObject *conn);

void connection_handle_error(ConnectionObject *conn);

int connection_run_without_results(ConnectionObject *conn, const char *query);

int connection_run(ConnectionObject *conn, const char *query, PyObject *params,
                   PyObject **columns);

int connection_pull(ConnectionObject *conn, PyObject **row);

int connection_begin(ConnectionObject *conn);

void connection_discard_all(ConnectionObject *conn);

#endif
