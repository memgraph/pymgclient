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

#ifndef PYMGCLIENT_ROUTER_H
#define PYMGCLIENT_ROUTER_H

#include <Python.h>

// A thin Python wrapper over libmgclient's `mg_router` (client-side routing
// engine). Not part of the public API surface directly; the ergonomic
// `mgclient.routing.Router` facade is built on top of it.
extern PyTypeObject RouterType;

#endif  // PYMGCLIENT_ROUTER_H
