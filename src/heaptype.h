// Copyright (c) 2016-2026 Memgraph Ltd. [https://memgraph.com]
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

#ifndef PYMGCLIENT_HEAPTYPE_H
#define PYMGCLIENT_HEAPTYPE_H

#include <Python.h>

// Helpers for the heap types created from a PyType_Spec. Under the limited
// API PyTypeObject is opaque, so slots have to be looked up with
// PyType_GetSlot instead of being read from the struct.

static inline PyObject *heap_type_alloc(PyTypeObject *type) {
  allocfunc tp_alloc = (allocfunc)PyType_GetSlot(type, Py_tp_alloc);
  return tp_alloc(type, 0);
}

// Frees `self` and releases the reference every heap type instance holds on
// its type. Must be the last thing done by a tp_dealloc.
static inline void heap_type_free(void *self) {
  PyTypeObject *type = Py_TYPE((PyObject *)self);
  freefunc tp_free = (freefunc)PyType_GetSlot(type, Py_tp_free);
  tp_free(self);
  Py_DECREF(type);
}

#endif
