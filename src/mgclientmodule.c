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

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#define APILEVEL "2.0"
#define THREADSAFETY 1
// For simplicity, here we deviate from the DB-API spec.
#define PARAMSTYLE "cypher"

#include <mgclient.h>

#include "column.h"
#include "connection.h"
#include "cursor.h"
#include "glue.h"
#include "types.h"

PyObject *Warning;
PyObject *Error;
PyObject *InterfaceError;
PyObject *DatabaseError;
PyObject *DataError;
PyObject *OperationalError;
PyObject *IntegrityError;
PyObject *InternalError;
PyObject *ProgrammingError;
PyObject *NotSupportedError;

PyDoc_STRVAR(Warning_doc, "Exception raised for important warnings.");
PyDoc_STRVAR(Error_doc, "Base class of all other error exceptions.");
PyDoc_STRVAR(
    InterfaceError_doc,
    "Exception raised for errors related to the database interface rather than "
    "the database itself.");
PyDoc_STRVAR(DatabaseError_doc,
             "Exception raised for errors related to the database.");
PyDoc_STRVAR(
    DataError_doc,
    "Exception raised for errors that are due to problems with the processed "
    "data.");
PyDoc_STRVAR(
    OperationalError_doc,
    "Exception raised for errors related to the database's operation, not "
    "necessarily under the control of the programmer (e.g. unexpected "
    "disconnect, failed allocation).");
PyDoc_STRVAR(
    IntegrityError_doc,
    "Exception raised when the relational integrity of the database is "
    "affected.");
PyDoc_STRVAR(
    InternalError_doc,
    "Exception raised when the database encounters an internal error.");
PyDoc_STRVAR(
    ProgrammingError_doc,
    "Exception raised for programming errors (e.g. syntax error, invalid "
    "parameters)");
PyDoc_STRVAR(
    NotSupportedError_doc,
    "Exception raised in a case a method or database API was used which is not "
    "supported by the database.");

int add_module_exceptions(PyObject *module) {
  struct {
    const char *name;
    PyObject **exc;
    PyObject **base;
    const char *docstring;
  } module_exceptions[] = {
      {"mgclient.Warning", &Warning, &PyExc_Exception, Warning_doc},
      {"mgclient.Error", &Error, &PyExc_Exception, Error_doc},
      {"mgclient.InterfaceError", &InterfaceError, &Error, InterfaceError_doc},
      {"mgclient.DatabaseError", &DatabaseError, &Error, DatabaseError_doc},
      {"mgclient.DataError", &DataError, &DatabaseError, DataError_doc},
      {"mgclient.OperationalError", &OperationalError, &DatabaseError,
       OperationalError_doc},
      {"mgclient.IntegrityError", &IntegrityError, &DatabaseError,
       IntegrityError_doc},
      {"mgclient.InternalError", &InternalError, &DatabaseError,
       InternalError_doc},
      {"mgclient.ProgrammingError", &ProgrammingError, &DatabaseError,
       ProgrammingError_doc},
      {"mgclient.NotSupportedError", &NotSupportedError, &DatabaseError,
       NotSupportedError_doc},
      {NULL, NULL, NULL, NULL}};

  for (size_t i = 0; module_exceptions[i].name; ++i) {
    *module_exceptions[i].exc = NULL;
  }
  for (size_t i = 0; module_exceptions[i].name; ++i) {
    PyObject *exc = PyErr_NewExceptionWithDoc(module_exceptions[i].name,
                                              module_exceptions[i].docstring,
                                              *module_exceptions[i].base, NULL);
    if (!exc) {
      goto cleanup;
    }
    *module_exceptions[i].exc = exc;
  }
  for (size_t i = 0; module_exceptions[i].name; ++i) {
    const char *name = strrchr(module_exceptions[i].name, '.');
    name = name ? name + 1 : module_exceptions[i].name;
    if (PyModule_AddObject(module, name, *module_exceptions[i].exc) < 0) {
      goto cleanup;
    }
  }

  return 0;

cleanup:
  for (size_t i = 0; module_exceptions[i].name; ++i) {
    Py_XDECREF(*module_exceptions[i].exc);
  }
  return -1;
}

int add_module_constants(PyObject *module) {
  if (PyModule_AddStringConstant(module, "apilevel", APILEVEL) < 0) {
    return -1;
  }
  if (PyModule_AddIntConstant(module, "threadsafety", THREADSAFETY) < 0) {
    return -1;
  }
  if (PyModule_AddStringConstant(module, "paramstyle", PARAMSTYLE) < 0) {
    return -1;
  }
  if (PyModule_AddIntMacro(module, MG_SSLMODE_REQUIRE) < 0) {
    return -1;
  }
  if (PyModule_AddIntMacro(module, MG_SSLMODE_DISABLE) < 0) {
    return -1;
  }

  // Connection status constants.
  if (PyModule_AddIntMacro(module, CONN_STATUS_READY) < 0) {
    return -1;
  }
  if (PyModule_AddIntMacro(module, CONN_STATUS_BAD) < 0) {
    return -1;
  }
  if (PyModule_AddIntMacro(module, CONN_STATUS_CLOSED) < 0) {
    return -1;
  }
  if (PyModule_AddIntMacro(module, CONN_STATUS_IN_TRANSACTION) < 0) {
    return -1;
  }
  if (PyModule_AddIntMacro(module, CONN_STATUS_EXECUTING) < 0) {
    return -1;
  }

  return 0;
}

static struct {
  char *name;
  PyTypeObject *type;
} type_table[] = {{"Connection", &ConnectionType},
                  {"Cursor", &CursorType},
                  {"Column", &ColumnType},
                  {"Node", &NodeType},
                  {"Relationship", &RelationshipType},
                  {"Path", &PathType},
                  {"Point2D", &Point2DType},
                  {NULL, NULL}};

static int add_module_types(PyObject *module) {
  for (size_t i = 0; type_table[i].name; ++i) {
    if (PyType_Ready(type_table[i].type) < 0) {
      return -1;
    }
    if (PyModule_AddObject(module, type_table[i].name,
                           (PyObject *)type_table[i].type) < 0) {
      return -1;
    }
  }
  return 0;
}

static PyObject *mgclient_connect(PyObject *self, PyObject *args,
                                  PyObject *kwargs) {
  // Unused parameter.
  (void)self;

  return PyObject_Call((PyObject *)&ConnectionType, args, kwargs);
}

// clang-format off
PyDoc_STRVAR(mgclient_connect_doc,
"connect(host=None, address=None, port=None, username=None, password=None,\n\
         client_name=None, sslmode=mgclient.MG_SSLMODE_DISABLE,\n\
         sslcert=None, sslkey=None, trust_callback=None, lazy=False)\n\
--\n\
\n\
Makes a new connection to the database server and returns a\n\
:class:`Connection` object.\n\
\n\
Currently recognized parameters are:\n\
\n\
   * :obj:`host`\n\
\n\
        DNS resolvable name of host to connect to. Exactly one of host and\n\
        address parameters must be specified.\n\
\n\
   * :obj:`address`\n\
\n\
        Numeric IP address of host to connect to. This should be in the\n\
        standard IPv4 address format. You can also use IPv6 if your machine\n\
        supports it. Exactly one of host and address parameters must be\n\
        specified.\n\
\n\
   * :obj:`port`\n\
\n\
        Port number to connect to at the server host.\n\
\n\
   * :obj:`username`\n\
\n\
        Username to connect as.\n\
\n\
   * :obj:`password`\n\
\n\
        Password to be used if the server demands password authentication.\n\
\n\
   * :obj:`client_name`\n\
\n\
         Alternate name and version of the client to send to server. Default is\n\
         set by the underlying mgclient library.\n\
\n\
   * :obj:`sslmode`\n\
\n\
        This option determines whether a secure connection will be negotiated\n\
        with the server. There are 2 possible values:\n\
\n\
           * :const:`mgclient.MG_SSLMODE_DISABLE`\n\
\n\
                Only try a non-SSL connection (default).\n\
\n\
           * :const:`mgclient.MG_SSLMODE_REQUIRE`\n\
\n\
                Only try an SSL connection.\n\
\n\
   * :obj:`sslcert`\n\
\n\
        This parameter specifies the file name of the client SSL certificate.\n\
        It is ignored in case an SSL connection is not made.\n\
\n\
   * :obj:`sslkey`\n\
\n\
        This parameter specifies the location of the secret key used for the\n\
        client certificate. This parameter is ignored in case an SSL connection\n\
        is not made.\n\
\n\
   * :obj:`trust_callback`\n\
\n\
        A callable taking four arguments.\n\
\n\
        After performing the SSL handshake, :meth:`connect` will call this\n\
        callable providing the hostname, IP address, public key type and\n\
        fingerprint. If the function returns ``False`` SSL connection will\n\
        immediately be terminated.\n\
\n\
        This can be used to implement TOFU (trust on first use) mechanism.\n\
\n\
   * :obj:`lazy`\n\
\n\
        If this is set to ``True``, a lazy connection is made. Default is ``False``.");
// clang-format on

static PyMethodDef mgclient_methods[] = {
    {"connect", (PyCFunction)mgclient_connect, METH_VARARGS | METH_KEYWORDS,
     mgclient_connect_doc},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef mgclient_module = {.m_base = PyModuleDef_HEAD_INIT,
                                             .m_name = "mgclient",
                                             .m_doc = NULL,
                                             .m_size = -1,
                                             .m_methods = mgclient_methods,
                                             .m_slots = NULL};

PyMODINIT_FUNC PyInit_mgclient(void) {
  PyObject *m;
  if (!(m = PyModule_Create(&mgclient_module))) {
    return NULL;
  }
  if (add_module_exceptions(m) < 0) {
    return NULL;
  }
  if (add_module_constants(m) < 0) {
    return NULL;
  }
  if (add_module_types(m) < 0) {
    return NULL;
  }
  if (mg_init() != MG_SUCCESS) {
    return NULL;
  }

  py_datetime_import_init();
  return m;
}
