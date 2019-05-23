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

#ifndef PY_MG_CLIENT_GLUE_H
#define PY_MG_CLIENT_GLUE_H

#include <Python.h>

#include <mgclient.h>

PyObject *mg_list_to_py_tuple(const mg_list *list);

PyObject *mg_list_to_py_list(const mg_list *list);

PyObject *mg_value_to_py_object(const mg_value *value);

mg_map *py_dict_to_mg_map(PyObject *dict);

mg_value *py_object_to_mg_value(PyObject *object);

#endif
