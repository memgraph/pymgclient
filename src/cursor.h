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

#ifndef PYMGCLIENT_CURSOR_H
#define PYMGCLIENT_CURSOR_H

#include <Python.h>

struct ConnectionObject;

#define CURSOR_STATUS_READY 0
#define CURSOR_STATUS_EXECUTING 1
#define CURSOR_STATUS_CLOSED 2

// clang-format off
typedef struct {
  PyObject_HEAD

  struct ConnectionObject *conn;
  int status;
  int hasresults;
  long arraysize;

  Py_ssize_t rowindex;
  Py_ssize_t rowcount;
  PyObject *rows;
  PyObject *description;
} CursorObject;
// clang-format on

extern PyTypeObject CursorType;

#endif
