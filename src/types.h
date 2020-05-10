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

#ifndef PYMGCLIENT_TYPES_H
#define PYMGCLIENT_TYPES_H

#include <Python.h>

// clang-format off
typedef struct {
  PyObject_HEAD

  int64_t id;
  PyObject *labels;
  PyObject *properties;
} NodeObject;

typedef struct {
  PyObject_HEAD

  int64_t id;
  int64_t start_id;
  int64_t end_id;
  PyObject *type;
  PyObject *properties;
} RelationshipObject;

typedef struct {
  PyObject_HEAD

  PyObject *nodes;
  PyObject *relationships;
} PathObject;
// clang-format on

extern PyTypeObject NodeType;
extern PyTypeObject RelationshipType;
extern PyTypeObject PathType;

#endif
