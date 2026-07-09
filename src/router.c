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

#include "router.h"

#include <mgclient.h>

#include "connection.h"
#include "exceptions.h"
#include "glue.h"

// clang-format off
typedef struct RouterObject {
  PyObject_HEAD

  mg_router *router;
  // The Python address resolver (or NULL for the identity mapping). Borrowed by
  // `router` as its resolver_data, so it must outlive `router`.
  PyObject *resolver;
  // The most recent exception raised inside a callback (resolver or work),
  // stashed while control is down in libmgclient's C code and re-raised once
  // the top-level call returns. Everything here runs single-threaded with the
  // GIL held, so a per-router slot is safe.
  PyObject *exc_type;
  PyObject *exc_value;
  PyObject *exc_tb;
} RouterObject;
// clang-format on

// -- callback exception stashing --------------------------------------------

static void router_clear_stashed(RouterObject *self) {
  Py_CLEAR(self->exc_type);
  Py_CLEAR(self->exc_value);
  Py_CLEAR(self->exc_tb);
}

// Move the currently-set Python exception into the router's stash (clearing the
// pending error so libmgclient's C code runs without one set).
static void router_stash_exception(RouterObject *self) {
  router_clear_stashed(self);
  PyErr_Fetch(&self->exc_type, &self->exc_value, &self->exc_tb);
}

static int router_has_stashed(const RouterObject *self) {
  return self->exc_type != NULL || self->exc_value != NULL;
}

// Raise a Python exception for a failed router operation that returned `status`.
// A stashed callback exception takes precedence (it carries the real cause and
// traceback); otherwise the router's own message is used, classified as
// transient or not.
static void router_raise(RouterObject *self, int status) {
  if (router_has_stashed(self)) {
    PyErr_Restore(self->exc_type, self->exc_value, self->exc_tb);
    self->exc_type = self->exc_value = self->exc_tb = NULL;
    return;
  }
  PyObject *exc =
      mg_error_is_transient(status) ? TransientError : OperationalError;
  PyErr_SetString(exc, mg_router_error(self->router));
}

// -- resolver trampoline -----------------------------------------------------

// Bridges libmgclient's `mg_resolver_fn` to the Python resolver callable, which
// maps an advertised "host:port" to an iterable of "host:port" targets.
static int router_resolver_trampoline(const char *advertised,
                                      mg_resolver_result *result, void *data) {
  RouterObject *self = (RouterObject *)data;

  PyObject *targets = PyObject_CallFunction(self->resolver, "s", advertised);
  if (!targets) {
    router_stash_exception(self);
    return MG_ERROR_CLIENT_ERROR;
  }
  PyObject *seq =
      PySequence_Fast(targets, "resolver must return an iterable of addresses");
  Py_DECREF(targets);
  if (!seq) {
    router_stash_exception(self);
    return MG_ERROR_CLIENT_ERROR;
  }

  int rc = 0;
  Py_ssize_t size = PySequence_Fast_GET_SIZE(seq);
  for (Py_ssize_t i = 0; i < size; ++i) {
    PyObject *item = PySequence_Fast_GET_ITEM(seq, i);  // borrowed
    const char *target = PyUnicode_AsUTF8(item);
    if (!target) {
      router_stash_exception(self);
      rc = MG_ERROR_CLIENT_ERROR;
      break;
    }
    if (mg_resolver_result_add(result, target) != 0) {
      PyErr_NoMemory();
      router_stash_exception(self);
      rc = MG_ERROR_OOM;
      break;
    }
  }
  Py_DECREF(seq);
  return rc;
}

// -- lifecycle ---------------------------------------------------------------

static int router_init(RouterObject *self, PyObject *args, PyObject *kwargs) {
  static char *kwlist[] = {"host",
                           "address",
                           "port",
                           "username",
                           "password",
                           "client_name",
                           "sslmode",
                           "sslcert",
                           "sslkey",
                           "resolver",
                           "routing_context",
                           "max_retries",
                           "retry_backoff",
                           "retry_backoff_cap",
                           NULL};

  const char *host = NULL;
  const char *address = NULL;
  int port = -1;
  const char *username = NULL;
  const char *password = NULL;
  const char *client_name = NULL;
  int sslmode_int = MG_SSLMODE_DISABLE;
  const char *sslcert = NULL;
  const char *sslkey = NULL;
  PyObject *resolver = NULL;
  PyObject *routing_context = NULL;
  unsigned int max_retries = 8;
  double retry_backoff = 1.0;
  double retry_backoff_cap = 15.0;

  if (!PyArg_ParseTupleAndKeywords(
          args, kwargs, "|$zzizzzizzOOIdd", kwlist, &host, &address, &port,
          &username, &password, &client_name, &sslmode_int, &sslcert, &sslkey,
          &resolver, &routing_context, &max_retries, &retry_backoff,
          &retry_backoff_cap)) {
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

  if (resolver && resolver != Py_None && !PyCallable_Check(resolver)) {
    PyErr_SetString(PyExc_TypeError, "resolver argument must be callable");
    return -1;
  }
  if (routing_context && routing_context != Py_None &&
      !PyDict_Check(routing_context)) {
    PyErr_SetString(PyExc_TypeError, "routing_context must be a dict");
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
  mg_session_params_set_username(params, username);
  mg_session_params_set_password(params, password);
  if (client_name) {
    mg_session_params_set_user_agent(params, client_name);
  }
  mg_session_params_set_sslmode(params, sslmode);
  mg_session_params_set_sslcert(params, sslcert);
  mg_session_params_set_sslkey(params, sslkey);

  mg_map *mg_routing_context = NULL;
  if (routing_context && routing_context != Py_None) {
    mg_routing_context = py_dict_to_mg_map(routing_context);
    if (!mg_routing_context) {
      mg_session_params_destroy(params);
      return -1;
    }
  }

  mg_router_config *config = mg_router_config_make();
  if (!config) {
    mg_session_params_destroy(params);
    mg_map_destroy(mg_routing_context);
    PyErr_SetString(PyExc_RuntimeError, "couldn't allocate router config");
    return -1;
  }
  mg_router_config_set_session_params(config, params);
  if (mg_routing_context) {
    mg_router_config_set_routing_context(config, mg_routing_context);
  }
  mg_router_config_set_max_retries(config, (uint32_t)max_retries);
  mg_router_config_set_retry_backoff(config, retry_backoff, retry_backoff_cap);

  PyObject *stored_resolver = NULL;
  if (resolver && resolver != Py_None) {
    stored_resolver = resolver;
    Py_INCREF(stored_resolver);
    mg_router_config_set_resolver(config, router_resolver_trampoline, self);
  }

  mg_router *router = mg_router_make(config);
  mg_session_params_destroy(params);
  mg_map_destroy(mg_routing_context);
  mg_router_config_destroy(config);

  if (!router) {
    Py_XDECREF(stored_resolver);
    PyErr_SetString(PyExc_RuntimeError, "couldn't create router");
    return -1;
  }

  // Replace any previous state (in case __init__ is called twice).
  mg_router_destroy(self->router);
  Py_XDECREF(self->resolver);
  router_clear_stashed(self);
  self->router = router;
  self->resolver = stored_resolver;
  return 0;
}

static PyObject *router_new(PyTypeObject *subtype, PyObject *args,
                            PyObject *kwargs) {
  (void)args;
  (void)kwargs;
  RouterObject *self = (RouterObject *)subtype->tp_alloc(subtype, 0);
  if (!self) {
    return NULL;
  }
  self->router = NULL;
  self->resolver = NULL;
  self->exc_type = self->exc_value = self->exc_tb = NULL;
  return (PyObject *)self;
}

static void router_dealloc(RouterObject *self) {
  // Destroy the router first: it borrows `resolver` as its resolver_data.
  mg_router_destroy(self->router);
  Py_XDECREF(self->resolver);
  router_clear_stashed(self);
  Py_TYPE(self)->tp_free(self);
}

// -- connect -----------------------------------------------------------------

static PyObject *router_connect_role(RouterObject *self, int write) {
  router_clear_stashed(self);
  mg_session *session = NULL;
  int status = write ? mg_router_connect_write(self->router, &session)
                     : mg_router_connect_read(self->router, &session);
  if (status != 0) {
    router_raise(self, status);
    return NULL;
  }
  // The caller owns the returned connection; it owns and closes the session.
  PyObject *conn = connection_wrap_session(session, /*owns_session=*/1,
                                           /*autocommit=*/0);
  if (!conn) {
    mg_session_destroy(session);
    return NULL;
  }
  return conn;
}

PyDoc_STRVAR(router_connect_read_doc,
             "connect_read()\n--\n\n"
             "Open an owning Connection to a server that serves reads.");

static PyObject *router_connect_read(RouterObject *self, PyObject *args) {
  (void)args;
  return router_connect_role(self, /*write=*/0);
}

PyDoc_STRVAR(router_connect_write_doc,
             "connect_write()\n--\n\n"
             "Open an owning Connection to the server that serves writes.");

static PyObject *router_connect_write(RouterObject *self, PyObject *args) {
  (void)args;
  return router_connect_role(self, /*write=*/1);
}

// -- managed transactions ----------------------------------------------------

struct work_ctx {
  RouterObject *self;
  PyObject *work;    // borrowed
  PyObject *result;  // owned; the value returned by the last successful work
};

// Bridges libmgclient's `mg_work_fn` to the Python work callable, which
// receives a Cursor and returns the caller's result. A raised exception is
// stashed and mapped to a transient/non-transient status so the retry loop in
// libmgclient can decide whether to try again.
static int router_work_trampoline(mg_session *session, void *data) {
  struct work_ctx *ctx = (struct work_ctx *)data;
  RouterObject *self = ctx->self;

  // A fresh attempt: drop any exception stashed by a previous one.
  router_clear_stashed(self);

  // The work runs against a borrowed connection in autocommit mode: for a
  // write, libmgclient owns the BEGIN/COMMIT around this callback, so the
  // Python layer must not drive the transaction itself.
  PyObject *conn = connection_wrap_session(session, /*owns_session=*/0,
                                           /*autocommit=*/1);
  if (!conn) {
    router_stash_exception(self);
    return MG_ERROR_CLIENT_ERROR;
  }
  PyObject *cursor = PyObject_CallMethod(conn, "cursor", NULL);
  if (!cursor) {
    router_stash_exception(self);
    Py_DECREF(conn);
    return MG_ERROR_CLIENT_ERROR;
  }

  PyObject *result = PyObject_CallFunctionObjArgs(ctx->work, cursor, NULL);
  Py_DECREF(cursor);
  Py_DECREF(conn);

  if (!result) {
    int transient = PyErr_ExceptionMatches(TransientError);
    router_stash_exception(self);
    return transient ? MG_ERROR_TRANSIENT_ERROR : MG_ERROR_CLIENT_ERROR;
  }
  Py_XSETREF(ctx->result, result);
  return 0;
}

static PyObject *router_execute_role(RouterObject *self, PyObject *work,
                                     int write) {
  if (!PyCallable_Check(work)) {
    PyErr_SetString(PyExc_TypeError, "work argument must be callable");
    return NULL;
  }
  router_clear_stashed(self);

  struct work_ctx ctx = {.self = self, .work = work, .result = NULL};
  int status =
      write ? mg_router_execute_write(self->router, router_work_trampoline,
                                      &ctx)
            : mg_router_execute_read(self->router, router_work_trampoline,
                                     &ctx);

  if (status != 0) {
    Py_XDECREF(ctx.result);
    router_raise(self, status);
    return NULL;
  }
  // Success: discard any exception stashed by an earlier, retried attempt.
  router_clear_stashed(self);
  if (ctx.result == NULL) {
    Py_RETURN_NONE;
  }
  return ctx.result;  // reference transferred to the caller
}

PyDoc_STRVAR(router_execute_read_doc,
             "execute_read(work)\n--\n\n"
             "Run work(cursor) as a managed read against a replica.");

static PyObject *router_execute_read(RouterObject *self, PyObject *work) {
  return router_execute_role(self, work, /*write=*/0);
}

PyDoc_STRVAR(router_execute_write_doc,
             "execute_write(work)\n--\n\n"
             "Run work(cursor) as a managed write against the main.");

static PyObject *router_execute_write(RouterObject *self, PyObject *work) {
  return router_execute_role(self, work, /*write=*/1);
}

// -- refresh / routing table -------------------------------------------------

PyDoc_STRVAR(router_refresh_doc,
             "refresh()\n--\n\n"
             "Force an immediate refresh of the cached routing table.");

static PyObject *router_refresh(RouterObject *self, PyObject *args) {
  (void)args;
  router_clear_stashed(self);
  int status = mg_router_refresh(self->router);
  if (status != 0) {
    router_raise(self, status);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject *role_addresses(const mg_routing_table *table,
                                enum mg_routing_role role) {
  uint32_t count = mg_routing_table_address_count(table, role);
  PyObject *list = PyList_New(count);
  if (!list) {
    return NULL;
  }
  for (uint32_t i = 0; i < count; ++i) {
    PyObject *item =
        PyUnicode_FromString(mg_routing_table_address_at(table, role, i));
    if (!item) {
      Py_DECREF(list);
      return NULL;
    }
    PyList_SET_ITEM(list, i, item);  // steals reference
  }
  return list;
}

PyDoc_STRVAR(router_routing_table_doc,
             "routing_table()\n--\n\n"
             "Return the cached routing table (refreshing it if none is "
             "cached yet) as a dict with 'ttl', 'write', 'read' and 'route'.");

static PyObject *router_routing_table(RouterObject *self, PyObject *args) {
  (void)args;
  router_clear_stashed(self);

  if (mg_router_routing_table(self->router) == NULL) {
    int status = mg_router_refresh(self->router);
    if (status != 0) {
      router_raise(self, status);
      return NULL;
    }
  }
  const mg_routing_table *table = mg_router_routing_table(self->router);
  if (!table) {
    PyErr_SetString(TransientError, "no routing table available");
    return NULL;
  }

  PyObject *write = role_addresses(table, MG_ROUTING_ROLE_WRITE);
  PyObject *read = role_addresses(table, MG_ROUTING_ROLE_READ);
  PyObject *route = role_addresses(table, MG_ROUTING_ROLE_ROUTE);
  PyObject *ttl =
      PyLong_FromLongLong((long long)mg_routing_table_ttl(table));
  PyObject *dict = NULL;
  if (write && read && route && ttl) {
    dict = PyDict_New();
  }
  if (!dict) {
    Py_XDECREF(write);
    Py_XDECREF(read);
    Py_XDECREF(route);
    Py_XDECREF(ttl);
    return NULL;
  }
  int ok = PyDict_SetItemString(dict, "ttl", ttl) == 0 &&
           PyDict_SetItemString(dict, "write", write) == 0 &&
           PyDict_SetItemString(dict, "read", read) == 0 &&
           PyDict_SetItemString(dict, "route", route) == 0;
  Py_DECREF(write);
  Py_DECREF(read);
  Py_DECREF(route);
  Py_DECREF(ttl);
  if (!ok) {
    Py_DECREF(dict);
    return NULL;
  }
  return dict;
}

static PyMethodDef router_methods[] = {
    {"connect_read", (PyCFunction)router_connect_read, METH_NOARGS,
     router_connect_read_doc},
    {"connect_write", (PyCFunction)router_connect_write, METH_NOARGS,
     router_connect_write_doc},
    {"execute_read", (PyCFunction)router_execute_read, METH_O,
     router_execute_read_doc},
    {"execute_write", (PyCFunction)router_execute_write, METH_O,
     router_execute_write_doc},
    {"refresh", (PyCFunction)router_refresh, METH_NOARGS, router_refresh_doc},
    {"routing_table", (PyCFunction)router_routing_table, METH_NOARGS,
     router_routing_table_doc},
    {NULL, NULL, 0, NULL}};

PyDoc_STRVAR(RouterType_doc,
             "Low-level client-side routing engine over libmgclient's "
             "mg_router. Use mgclient.routing.Router instead.");

// clang-format off
PyTypeObject RouterType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "mgclient._mgclient._Router",
    .tp_doc = RouterType_doc,
    .tp_basicsize = sizeof(RouterObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_dealloc = (destructor)router_dealloc,
    .tp_methods = router_methods,
    .tp_init = (initproc)router_init,
    .tp_new = (newfunc)router_new};
// clang-format on
