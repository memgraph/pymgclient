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

#include "glue.h"

#include "types.h"

#include <Python.h>
#include <datetime.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/**
 * TODO(colinbarry) Python 3.9 doesn't support the PyDateTime_DATE_GET_TZINFO
 * macro. Until we require Python 3.10, this shim function does exactly the
 * same thing whilst keeping 3.9 compatibility.
 */
PyObject* INTERNAL_PyDateTime_DATE_GET_TZINFO(PyObject *obj)
{
    PyObject *tzinfo = NULL;

    if (PyDateTime_Check(obj)) {
        tzinfo = ((PyDateTime_DateTime*)obj)->tzinfo;
    } else if (PyTime_Check(obj)) {
        tzinfo = ((PyDateTime_Time*)obj)->tzinfo;
    }

    return tzinfo ? tzinfo : Py_None;
}

void py_datetime_import_init() { PyDateTime_IMPORT; }

PyObject *mg_list_to_py_tuple(const mg_list *list) {
  PyObject *tuple = PyTuple_New(mg_list_size(list));
  if (!tuple) {
    return NULL;
  }
  for (uint32_t i = 0; i < mg_list_size(list); ++i) {
    PyObject *elem = mg_value_to_py_object(mg_list_at(list, i));
    if (!elem) {
      goto cleanup;
    }
    PyTuple_SET_ITEM(tuple, i, elem);
  }

  return tuple;

cleanup:
  Py_DECREF(tuple);
  return NULL;
}

PyObject *mg_string_to_py_unicode(const mg_string *str) {
  return PyUnicode_FromStringAndSize(mg_string_data(str), mg_string_size(str));
}

PyObject *mg_list_to_py_list(const mg_list *list) {
  PyObject *pylist = PyList_New(mg_list_size(list));
  if (!pylist) {
    return NULL;
  }
  for (uint32_t i = 0; i < mg_list_size(list); ++i) {
    PyObject *elem = mg_value_to_py_object(mg_list_at(list, i));
    if (!elem) {
      goto cleanup;
    }
    PyList_SET_ITEM(pylist, i, elem);
  }

  return pylist;

cleanup:
  Py_DECREF(pylist);
  return NULL;
}

PyObject *mg_map_to_py_dict(const mg_map *map) {
  PyObject *dict = PyDict_New();
  if (!dict) {
    return NULL;
  }
  for (uint32_t i = 0; i < mg_map_size(map); ++i) {
    PyObject *key = mg_string_to_py_unicode(mg_map_key_at(map, i));
    PyObject *value = mg_value_to_py_object(mg_map_value_at(map, i));
    if (!key || !value) {
      Py_XDECREF(key);
      Py_XDECREF(value);
      goto cleanup;
    }
    int insert_status = PyDict_SetItem(dict, key, value);
    Py_DECREF(key);
    Py_DECREF(value);
    if (insert_status < 0) {
      goto cleanup;
    }
  }

  return dict;

cleanup:
  Py_DECREF(dict);
  return NULL;
}

PyObject *mg_node_to_py_node(const mg_node *node) {
  PyObject *label_list = NULL;
  PyObject *label_set = NULL;
  PyObject *props = NULL;
  PyObject *ret = NULL;

  if (!(label_list = PyList_New(mg_node_label_count(node)))) {
    goto exit;
  }
  for (uint32_t i = 0; i < mg_node_label_count(node); ++i) {
    PyObject *label = mg_string_to_py_unicode(mg_node_label_at(node, i));
    if (!label) {
      goto exit;
    }
    PyList_SET_ITEM(label_list, i, label);
  }

  if (!(label_set = PySet_New(label_list))) {
    goto exit;
  }

  if (!(props = mg_map_to_py_dict(mg_node_properties(node)))) {
    goto exit;
  }

  ret = PyObject_CallFunction((PyObject *)&NodeType, "LOO", mg_node_id(node),
                              label_set, props);

exit:
  Py_XDECREF(label_list);
  Py_XDECREF(label_set);
  Py_XDECREF(props);
  return ret;
}

PyObject *mg_relationship_to_py_relationship(const mg_relationship *rel) {
  PyObject *type = NULL;
  PyObject *props = NULL;
  PyObject *ret = NULL;

  if (!(type = mg_string_to_py_unicode(mg_relationship_type(rel)))) {
    goto exit;
  }
  if (!(props = mg_map_to_py_dict(mg_relationship_properties(rel)))) {
    goto exit;
  }

  ret = PyObject_CallFunction(
      (PyObject *)&RelationshipType, "LLLOO", mg_relationship_id(rel),
      mg_relationship_start_id(rel), mg_relationship_end_id(rel), type, props);

exit:
  Py_XDECREF(type);
  Py_XDECREF(props);
  return ret;
}

PyObject *mg_unbound_relationship_to_py_relationship(
    const mg_unbound_relationship *rel) {
  PyObject *type = NULL;
  PyObject *props = NULL;
  PyObject *ret = NULL;

  if (!(type = mg_string_to_py_unicode(mg_unbound_relationship_type(rel)))) {
    goto exit;
  }
  if (!(props = mg_map_to_py_dict(mg_unbound_relationship_properties(rel)))) {
    goto exit;
  }

  ret = PyObject_CallFunction((PyObject *)&RelationshipType, "LLLOO",
                              mg_unbound_relationship_id(rel), -1, -1, type,
                              props);

exit:
  Py_XDECREF(type);
  Py_XDECREF(props);
  return ret;
}

PyObject *mg_path_to_py_path(const mg_path *path) {
  PyObject *nodes = NULL;
  PyObject *rels = NULL;
  PyObject *ret = NULL;

  if (!(nodes = PyList_New(mg_path_length(path) + 1))) {
    goto exit;
  }
  if (!(rels = PyList_New(mg_path_length(path)))) {
    goto exit;
  }

  int64_t prev_node_id = -1;
  for (uint32_t i = 0; i <= mg_path_length(path); ++i) {
    int64_t curr_node_id = mg_node_id(mg_path_node_at(path, i));
    PyObject *node = mg_node_to_py_node(mg_path_node_at(path, i));
    if (!node) {
      goto exit;
    }
    PyList_SET_ITEM(nodes, i, node);
    if (i > 0) {
      PyObject *rel = mg_unbound_relationship_to_py_relationship(
          mg_path_relationship_at(path, i - 1));
      if (!rel) {
        goto exit;
      }
      if (mg_path_relationship_reversed_at(path, i - 1)) {
        ((RelationshipObject *)rel)->start_id = curr_node_id;
        ((RelationshipObject *)rel)->end_id = prev_node_id;
      } else {
        ((RelationshipObject *)rel)->start_id = prev_node_id;
        ((RelationshipObject *)rel)->end_id = curr_node_id;
      }
      PyList_SET_ITEM(rels, i - 1, rel);
    }
    prev_node_id = curr_node_id;
  }

  ret = PyObject_CallFunction((PyObject *)&PathType, "OO", nodes, rels);

exit:
  Py_XDECREF(nodes);
  Py_XDECREF(rels);
  return ret;
}

void maybe_decrement_ref(PyObject **obj) { Py_XDECREF(obj); }

#define SCOPED_CLEANUP __attribute__((cleanup(maybe_decrement_ref)))
#define IF_PTR_IS_NULL_RETURN(ptr, value) \
  do {                                    \
    if (!(ptr)) {                         \
      return (value);                     \
    }                                     \
  } while (false)

PyObject *make_py_date(int y, int m, int d) {
  PyObject *date = PyDate_FromDate(y, m, d);
  if (!date) {
    PyErr_Print();
  }
  return date;
}

PyObject *make_py_time(int64_t h, int64_t min, int64_t sec, int64_t mi) {
  PyObject *time = PyTime_FromTime(h, min, sec, mi);
  if (!time) {
    PyErr_Print();
  }
  return time;
}

PyObject *make_py_datetime(int y, int m, int d, int h, int min, int sec,
                           int mi) {
  PyObject *datetime = PyDateTime_FromDateAndTime(y, m, d, h, min, sec, mi);
  if (!datetime) {
    PyErr_Print();
  }
  return datetime;
}

PyObject *make_py_delta(int days, int sec, int micros) {
  PyObject *delta = PyDelta_FromDSU(days, sec, micros);
  if (!delta) {
    PyErr_Print();
  }
  return delta;
}

PyObject *mg_date_to_py_date(const mg_date *date) {
  SCOPED_CLEANUP PyObject *unix_epoch = make_py_date(1970, 1, 1);
  IF_PTR_IS_NULL_RETURN(unix_epoch, NULL);
  SCOPED_CLEANUP PyObject *date_as_delta =
      make_py_delta(mg_date_days(date), 0, 0);
  SCOPED_CLEANUP PyObject *method_name = PyUnicode_FromString("__add__");
  PyObject *result_date =
      PyObject_CallMethodObjArgs(unix_epoch, method_name, date_as_delta, NULL);
  if (!result_date) {
    PyErr_Print();
  }
  return result_date;
}

PyObject *mg_local_time_to_py_time(const mg_local_time *lt) {
  const int64_t nanos = mg_local_time_nanoseconds(lt);
  const int64_t one_sec_to_nanos = 1000000000;
  SCOPED_CLEANUP PyObject *seconds =
      PyLong_FromLongLong(nanos / one_sec_to_nanos);
  const int64_t leftover_nanos = nanos % one_sec_to_nanos;
  SCOPED_CLEANUP PyObject *method_name = PyUnicode_FromString("fromtimestamp");
  IF_PTR_IS_NULL_RETURN(method_name, NULL);
  SCOPED_CLEANUP PyObject *utc_result = PyObject_CallMethodObjArgs(
      (PyObject *)PyDateTimeAPI->DateTimeType, method_name, seconds,
      PyDateTime_TimeZone_UTC, NULL);
  IF_PTR_IS_NULL_RETURN(utc_result, NULL);
  SCOPED_CLEANUP PyObject *replace_method = PyObject_GetAttrString(utc_result, "replace");
  IF_PTR_IS_NULL_RETURN(replace_method, NULL);
  SCOPED_CLEANUP PyObject *tzinfo_kwarg = PyDict_New();
  IF_PTR_IS_NULL_RETURN(tzinfo_kwarg, NULL);
  PyDict_SetItemString(tzinfo_kwarg, "tzinfo", Py_None);
  SCOPED_CLEANUP PyObject *result = PyObject_Call(replace_method, PyTuple_New(0), tzinfo_kwarg);
  IF_PTR_IS_NULL_RETURN(result, NULL);
  SCOPED_CLEANUP PyObject *h = PyObject_GetAttrString(result, "hour");
  IF_PTR_IS_NULL_RETURN(h, NULL);
  SCOPED_CLEANUP PyObject *m = PyObject_GetAttrString(result, "minute");
  IF_PTR_IS_NULL_RETURN(m, NULL);
  SCOPED_CLEANUP PyObject *s = PyObject_GetAttrString(result, "second");
  IF_PTR_IS_NULL_RETURN(s, NULL);
  SCOPED_CLEANUP PyObject *mi = PyObject_GetAttrString(result, "microsecond");
  IF_PTR_IS_NULL_RETURN(mi, NULL);
  return make_py_time(PyLong_AsLong(h), PyLong_AsLong(m), PyLong_AsLong(s),
                      (leftover_nanos / 1000));
}

PyObject *mg_local_date_time_to_py_datetime(const mg_local_date_time *ldt) {
  SCOPED_CLEANUP PyObject *seconds =
      PyLong_FromLong(mg_local_date_time_seconds(ldt));
  IF_PTR_IS_NULL_RETURN(seconds, NULL);
  SCOPED_CLEANUP PyObject *method_name = PyUnicode_FromString("fromtimestamp");
  IF_PTR_IS_NULL_RETURN(method_name, NULL);
  SCOPED_CLEANUP PyObject *utc_result = PyObject_CallMethodObjArgs(
      (PyObject *)PyDateTimeAPI->DateTimeType, method_name, seconds,
      PyDateTime_TimeZone_UTC, NULL);
  IF_PTR_IS_NULL_RETURN(utc_result, NULL);
  SCOPED_CLEANUP PyObject *replace_method = PyObject_GetAttrString(utc_result, "replace");
  IF_PTR_IS_NULL_RETURN(replace_method, NULL);
  SCOPED_CLEANUP PyObject *tzinfo_kwarg = PyDict_New();
  IF_PTR_IS_NULL_RETURN(tzinfo_kwarg, NULL);
  PyDict_SetItemString(tzinfo_kwarg, "tzinfo", Py_None);
  SCOPED_CLEANUP PyObject *result = PyObject_Call(replace_method, PyTuple_New(0), tzinfo_kwarg);
  IF_PTR_IS_NULL_RETURN(result, NULL);
  SCOPED_CLEANUP PyObject *y = PyObject_GetAttrString(result, "year");
  SCOPED_CLEANUP PyObject *mo = PyObject_GetAttrString(result, "month");
  SCOPED_CLEANUP PyObject *d = PyObject_GetAttrString(result, "day");
  SCOPED_CLEANUP PyObject *h = PyObject_GetAttrString(result, "hour");
  SCOPED_CLEANUP PyObject *m = PyObject_GetAttrString(result, "minute");
  SCOPED_CLEANUP PyObject *s = PyObject_GetAttrString(result, "second");
  int64_t nanos = mg_local_date_time_nanoseconds(ldt);
  return make_py_datetime(PyLong_AsLong(y), PyLong_AsLong(mo), PyLong_AsLong(d),
                          PyLong_AsLong(h), PyLong_AsLong(m), PyLong_AsLong(s),
                          (nanos / 1000));
}

PyObject *mg_duration_to_py_delta(const mg_duration *dur) {
  int64_t days = mg_duration_days(dur);
  int64_t seconds = mg_duration_seconds(dur);
  int64_t nanoseconds = mg_duration_nanoseconds(dur);
  return make_py_delta(days, seconds, (nanoseconds / 1000));
}

PyObject *mg_date_time_to_py_datetime(const mg_date_time *dt) {
  int64_t seconds = mg_date_time_seconds(dt);
  int64_t nanoseconds = mg_date_time_nanoseconds(dt);
  int32_t tz_offset_minutes = mg_date_time_tz_offset_minutes(dt);

  SCOPED_CLEANUP PyObject *timestamp = PyLong_FromLongLong(seconds);
  IF_PTR_IS_NULL_RETURN(timestamp, NULL);

  SCOPED_CLEANUP PyObject *method_name = PyUnicode_FromString("fromtimestamp");
  IF_PTR_IS_NULL_RETURN(method_name, NULL);

  SCOPED_CLEANUP PyObject *utc_dt = PyObject_CallMethodObjArgs(
      (PyObject *)PyDateTimeAPI->DateTimeType, method_name, timestamp,
      PyDateTime_TimeZone_UTC, NULL);
  IF_PTR_IS_NULL_RETURN(utc_dt, NULL);

  SCOPED_CLEANUP PyObject *y = PyObject_GetAttrString(utc_dt, "year");
  SCOPED_CLEANUP PyObject *mo = PyObject_GetAttrString(utc_dt, "month");
  SCOPED_CLEANUP PyObject *d = PyObject_GetAttrString(utc_dt, "day");
  SCOPED_CLEANUP PyObject *h = PyObject_GetAttrString(utc_dt, "hour");
  SCOPED_CLEANUP PyObject *m = PyObject_GetAttrString(utc_dt, "minute");
  SCOPED_CLEANUP PyObject *s = PyObject_GetAttrString(utc_dt, "second");

  SCOPED_CLEANUP PyObject *datetime_module = PyImport_ImportModule("datetime");
  IF_PTR_IS_NULL_RETURN(datetime_module, NULL);

  SCOPED_CLEANUP PyObject *timezone_class = PyObject_GetAttrString(datetime_module, "timezone");
  IF_PTR_IS_NULL_RETURN(timezone_class, NULL);

  SCOPED_CLEANUP PyObject *timedelta_class = PyObject_GetAttrString(datetime_module, "timedelta");
  IF_PTR_IS_NULL_RETURN(timedelta_class, NULL);

  SCOPED_CLEANUP PyObject *offset_delta = PyObject_CallFunction(
      timedelta_class, "iii", 0, tz_offset_minutes * 60, 0);
  IF_PTR_IS_NULL_RETURN(offset_delta, NULL);

  SCOPED_CLEANUP PyObject *tz = PyObject_CallFunction(
      timezone_class, "O", offset_delta);
  IF_PTR_IS_NULL_RETURN(tz, NULL);

  SCOPED_CLEANUP PyObject *naive_dt = make_py_datetime(
      PyLong_AsLong(y), PyLong_AsLong(mo), PyLong_AsLong(d),
      PyLong_AsLong(h), PyLong_AsLong(m), PyLong_AsLong(s),
      (nanoseconds / 1000));
  IF_PTR_IS_NULL_RETURN(naive_dt, NULL);

  SCOPED_CLEANUP PyObject *replace_method = PyObject_GetAttrString(naive_dt, "replace");
  IF_PTR_IS_NULL_RETURN(replace_method, NULL);

  SCOPED_CLEANUP PyObject *tzinfo_kwarg = PyDict_New();
  IF_PTR_IS_NULL_RETURN(tzinfo_kwarg, NULL);
  PyDict_SetItemString(tzinfo_kwarg, "tzinfo", tz);

  return PyObject_Call(replace_method, PyTuple_New(0), tzinfo_kwarg);
}

PyObject *mg_date_time_zone_id_to_py_datetime(const mg_date_time_zone_id *dt) {
  int64_t seconds = mg_date_time_zone_id_seconds(dt);
  int64_t nanoseconds = mg_date_time_zone_id_nanoseconds(dt);
  const mg_string *timezone_name = mg_date_time_zone_id_timezone_name(dt);

  SCOPED_CLEANUP PyObject *timestamp = PyLong_FromLongLong(seconds);
  IF_PTR_IS_NULL_RETURN(timestamp, NULL);

  SCOPED_CLEANUP PyObject *method_name = PyUnicode_FromString("fromtimestamp");
  IF_PTR_IS_NULL_RETURN(method_name, NULL);

  SCOPED_CLEANUP PyObject *utc_dt = PyObject_CallMethodObjArgs(
      (PyObject *)PyDateTimeAPI->DateTimeType, method_name, timestamp,
      PyDateTime_TimeZone_UTC, NULL);
  IF_PTR_IS_NULL_RETURN(utc_dt, NULL);

  SCOPED_CLEANUP PyObject *y = PyObject_GetAttrString(utc_dt, "year");
  SCOPED_CLEANUP PyObject *mo = PyObject_GetAttrString(utc_dt, "month");
  SCOPED_CLEANUP PyObject *d = PyObject_GetAttrString(utc_dt, "day");
  SCOPED_CLEANUP PyObject *h = PyObject_GetAttrString(utc_dt, "hour");
  SCOPED_CLEANUP PyObject *m = PyObject_GetAttrString(utc_dt, "minute");
  SCOPED_CLEANUP PyObject *s = PyObject_GetAttrString(utc_dt, "second");

  SCOPED_CLEANUP PyObject *naive_dt = make_py_datetime(
      PyLong_AsLong(y), PyLong_AsLong(mo), PyLong_AsLong(d),
      PyLong_AsLong(h), PyLong_AsLong(m), PyLong_AsLong(s),
      (nanoseconds / 1000));
  IF_PTR_IS_NULL_RETURN(naive_dt, NULL);

  SCOPED_CLEANUP PyObject *zoneinfo_module = PyImport_ImportModule("zoneinfo");
  IF_PTR_IS_NULL_RETURN(zoneinfo_module, NULL);

  SCOPED_CLEANUP PyObject *zoneinfo_class = PyObject_GetAttrString(zoneinfo_module, "ZoneInfo");
  IF_PTR_IS_NULL_RETURN(zoneinfo_class, NULL);

  const char *tz_name_str = mg_string_data(timezone_name);
  SCOPED_CLEANUP PyObject *tz_name_py = PyUnicode_FromStringAndSize(tz_name_str, mg_string_size(timezone_name));
  IF_PTR_IS_NULL_RETURN(tz_name_py, NULL);

  SCOPED_CLEANUP PyObject *timezone_obj = PyObject_CallFunctionObjArgs(zoneinfo_class, tz_name_py, NULL);
  IF_PTR_IS_NULL_RETURN(timezone_obj, NULL);

  SCOPED_CLEANUP PyObject *replace_method = PyObject_GetAttrString(naive_dt, "replace");
  IF_PTR_IS_NULL_RETURN(replace_method, NULL);

  SCOPED_CLEANUP PyObject *tzinfo_kwarg = PyDict_New();
  IF_PTR_IS_NULL_RETURN(tzinfo_kwarg, NULL);
  PyDict_SetItemString(tzinfo_kwarg, "tzinfo", timezone_obj);

  return PyObject_Call(replace_method, PyTuple_New(0), tzinfo_kwarg);
}

PyObject *mg_value_to_py_object(const mg_value *value) {
  switch (mg_value_get_type(value)) {
    case MG_VALUE_TYPE_NULL:
      Py_RETURN_NONE;
    case MG_VALUE_TYPE_BOOL:
      if (mg_value_bool(value)) {
        Py_RETURN_TRUE;
      } else {
        Py_RETURN_FALSE;
      }
    case MG_VALUE_TYPE_INTEGER:
      return PyLong_FromLongLong(mg_value_integer(value));
    case MG_VALUE_TYPE_FLOAT:
      return PyFloat_FromDouble(mg_value_float(value));
    case MG_VALUE_TYPE_STRING:
      return mg_string_to_py_unicode(mg_value_string(value));
    case MG_VALUE_TYPE_LIST:
      return mg_list_to_py_list(mg_value_list(value));
    case MG_VALUE_TYPE_MAP:
      return mg_map_to_py_dict(mg_value_map(value));
    case MG_VALUE_TYPE_NODE:
      return mg_node_to_py_node(mg_value_node(value));
    case MG_VALUE_TYPE_RELATIONSHIP:
      return mg_relationship_to_py_relationship(mg_value_relationship(value));
    case MG_VALUE_TYPE_UNBOUND_RELATIONSHIP:
      return mg_unbound_relationship_to_py_relationship(
          mg_value_unbound_relationship(value));
    case MG_VALUE_TYPE_PATH:
      return mg_path_to_py_path(mg_value_path(value));
    case MG_VALUE_TYPE_DATE:
      return mg_date_to_py_date(mg_value_date(value));
    case MG_VALUE_TYPE_LOCAL_TIME:
      return mg_local_time_to_py_time(mg_value_local_time(value));
    case MG_VALUE_TYPE_LOCAL_DATE_TIME:
      return mg_local_date_time_to_py_datetime(mg_value_local_date_time(value));
    case MG_VALUE_TYPE_DATE_TIME:
      return mg_date_time_to_py_datetime(mg_value_date_time(value));
    case MG_VALUE_TYPE_DATE_TIME_ZONE_ID:
      return mg_date_time_zone_id_to_py_datetime(mg_value_date_time_zone_id(value));
    case MG_VALUE_TYPE_DURATION:
      return mg_duration_to_py_delta(mg_value_duration(value));
    default:
      PyErr_SetString(PyExc_RuntimeError,
                      "encountered a mg_value of unknown type");
      return NULL;
  }
}

mg_string *py_unicode_to_mg_string(PyObject *unicode) {
  assert(PyUnicode_Check(unicode));
  Py_ssize_t size;
  const char *data = PyUnicode_AsUTF8AndSize(unicode, &size);
  if (!data) {
    return NULL;
  }
  if (size > UINT32_MAX) {
    PyErr_SetString(PyExc_ValueError, "dictionary size exceeded");
    return NULL;
  }
  mg_string *ret = mg_string_make2((uint32_t)size, data);
  if (!ret) {
    PyErr_SetString(PyExc_RuntimeError, "failed to create a mg_string");
    return NULL;
  }
  return ret;
}

mg_list *py_list_to_mg_list(PyObject *pylist) {
  assert(PyList_Check(pylist));

  mg_list *list = NULL;

  if (PyList_Size(pylist) > UINT32_MAX) {
    PyErr_SetString(PyExc_ValueError, "list size exceeded");
    goto cleanup;
  }

  list = mg_list_make_empty((uint32_t)PyList_Size(pylist));
  if (!list) {
    PyErr_SetString(PyExc_RuntimeError, "failed to create a mg_list");
    goto cleanup;
  }

  for (uint32_t i = 0; i < (uint32_t)PyList_Size(pylist); ++i) {
    mg_value *elem = py_object_to_mg_value(PyList_GetItem(pylist, i));
    if (!elem) {
      return NULL;
    }
    if (mg_list_append(list, elem) != 0) {
      abort();
    }
  }

  return list;

cleanup:
  mg_list_destroy(list);
  return NULL;
}

mg_map *py_dict_to_mg_map(PyObject *dict) {
  assert(PyDict_Check(dict));

  mg_map *map = NULL;

  if (PyDict_Size(dict) > UINT32_MAX) {
    PyErr_SetString(PyExc_ValueError, "dictionary size exceeded");
    goto cleanup;
  }

  map = mg_map_make_empty((uint32_t)PyDict_Size(dict));
  if (!map) {
    PyErr_SetString(PyExc_RuntimeError, "failed to create a mg_map");
    goto cleanup;
  }

  Py_ssize_t pos = 0;
  PyObject *pykey;
  PyObject *pyvalue;
  while (PyDict_Next(dict, &pos, &pykey, &pyvalue)) {
    if (!PyUnicode_Check(pykey)) {
      PyErr_SetString(PyExc_ValueError, "dictionary key must be a string");
      goto cleanup;
    }
    mg_string *key = py_unicode_to_mg_string(pykey);
    if (!key) {
      goto cleanup;
    }

    mg_value *value = py_object_to_mg_value(pyvalue);
    if (!value) {
      mg_string_destroy(key);
      goto cleanup;
    }

    if (mg_map_insert_unsafe2(map, key, value) != 0) {
      abort();
    }
  }

  return map;

cleanup:
  mg_map_destroy(map);
  return NULL;
}

// Return 0 on failure
// Return 1 on success
int days_since_unix_epoch(int y, int m, int d, int64_t *result) {
  SCOPED_CLEANUP PyObject *unix_epoch =
      make_py_datetime(1970, 1, 1, 0, 0, 0, 0);
  IF_PTR_IS_NULL_RETURN(unix_epoch, 0);
  SCOPED_CLEANUP PyObject *date = make_py_datetime(y, m, d, 0, 0, 0, 0);
  IF_PTR_IS_NULL_RETURN(date, 0);
  SCOPED_CLEANUP PyObject *method_name = PyUnicode_FromString("__sub__");
  IF_PTR_IS_NULL_RETURN(method_name, 0);
  SCOPED_CLEANUP PyObject *delta =
      PyObject_CallMethodObjArgs(date, method_name, unix_epoch, NULL);
  IF_PTR_IS_NULL_RETURN(delta, 0);
  SCOPED_CLEANUP PyObject *days = PyObject_GetAttrString(delta, "days");
  IF_PTR_IS_NULL_RETURN(days, 0);
  *result = PyLong_AsLong(days);
  return 1;
}

int64_t microseconds_to_nanos(int64_t microseconds) {
  return microseconds * 1000;
}

int64_t seconds_to_nanos(int64_t seconds) { return seconds * 1000000 * 1000; }

int64_t minutes_to_nanos(int64_t minutes) {
  return seconds_to_nanos(minutes * 60);
}

int64_t hours_to_nanos(int64_t hours) { return minutes_to_nanos(hours * 60); }

int64_t nanoseconds_since_epoch(PyObject *obj) {
  int64_t h = PyDateTime_TIME_GET_HOUR(obj);
  int64_t m = PyDateTime_TIME_GET_MINUTE(obj);
  int64_t s = PyDateTime_TIME_GET_SECOND(obj);
  int64_t mi = PyDateTime_TIME_GET_MICROSECOND(obj);
  return hours_to_nanos(h) + minutes_to_nanos(m) + seconds_to_nanos(s) +
         microseconds_to_nanos(mi);
}

int64_t minutes_to_seconds(int64_t minutes) { return minutes * 60; }

int64_t hours_to_seconds(int hours) { return minutes_to_seconds(hours * 60); }

int64_t to_seconds(int64_t days, int hours, int minutes, int seconds) {
  // 1 day = 86400 seconds
  return days * 86400 + hours_to_seconds(hours) + minutes_to_seconds(minutes) +
         seconds;
}

// Return 0 on failure
// Return 1 on success
int seconds_since_unix_epoch(PyObject *obj, int64_t *result) {
  int y = PyDateTime_GET_YEAR(obj);
  int mo = PyDateTime_GET_MONTH(obj);
  int d = PyDateTime_GET_DAY(obj);
  int64_t days = 0;
  if (days_since_unix_epoch(y, mo, d, &days) == 0) {
    return 0;
  }
  int h = PyDateTime_DATE_GET_HOUR(obj);
  int m = PyDateTime_DATE_GET_MINUTE(obj);
  int s = PyDateTime_DATE_GET_SECOND(obj);
  *result = to_seconds(days, h, m, s);
  return 1;
}

int64_t subseconds_as_nanoseconds(PyObject *obj) {
  return microseconds_to_nanos(PyDateTime_DATE_GET_MICROSECOND(obj));
}

mg_date *py_date_to_mg_date(PyObject *obj) {
  int y = PyDateTime_GET_YEAR(obj);
  int m = PyDateTime_GET_MONTH(obj);
  int d = PyDateTime_GET_DAY(obj);
  int64_t days = 0;
  if (!days_since_unix_epoch(y, m, d, &days)) {
    return NULL;
  }
  return mg_date_make(days);
}

mg_local_time *py_time_to_mg_local_time(PyObject *obj) {
  return mg_local_time_make(nanoseconds_since_epoch(obj));
}

mg_local_date_time *py_date_time_to_mg_local_date_time(PyObject *obj) {
  int64_t seconds_since_epoch = 0;
  if (seconds_since_unix_epoch(obj, &seconds_since_epoch) == 0) {
    return NULL;
  }
  int64_t subseconds = subseconds_as_nanoseconds(obj);
  return mg_local_date_time_make(seconds_since_epoch, subseconds);
}

mg_date_time *py_date_time_to_mg_date_time(PyObject *obj) {
  int64_t seconds_since_epoch = 0;
  if (seconds_since_unix_epoch(obj, &seconds_since_epoch) == 0) {
    return NULL;
  }
  int64_t subseconds = subseconds_as_nanoseconds(obj);

  PyObject *tzinfo = INTERNAL_PyDateTime_DATE_GET_TZINFO(obj);
  if (tzinfo == Py_None) {
    return NULL;
  }

  SCOPED_CLEANUP PyObject *utc_offset = PyObject_CallMethod(tzinfo, "utcoffset", "O", obj);
  IF_PTR_IS_NULL_RETURN(utc_offset, NULL);

  SCOPED_CLEANUP PyObject *total_seconds = PyObject_CallMethod(utc_offset, "total_seconds", NULL);
  IF_PTR_IS_NULL_RETURN(total_seconds, NULL);

  double offset_seconds_double = PyFloat_AsDouble(total_seconds);
  int32_t offset_minutes = (int32_t)(offset_seconds_double / 60.0);

  return mg_date_time_make(seconds_since_epoch, subseconds, offset_minutes);
}

mg_date_time_zone_id *py_date_time_to_mg_date_time_zone_id(PyObject *obj) {
  int64_t seconds_since_epoch = 0;
  if (seconds_since_unix_epoch(obj, &seconds_since_epoch) == 0) {
    return NULL;
  }
  int64_t subseconds = subseconds_as_nanoseconds(obj);

  PyObject *tzinfo = INTERNAL_PyDateTime_DATE_GET_TZINFO(obj);
  if (tzinfo == Py_None) {
    return NULL;
  }

  SCOPED_CLEANUP PyObject *tzname_str = PyObject_Str(tzinfo);
  IF_PTR_IS_NULL_RETURN(tzname_str, NULL);

  if (!PyUnicode_Check(tzname_str)) {
    return NULL;
  }

  const char *timezone_name_str = PyUnicode_AsUTF8(tzname_str);
  if (!timezone_name_str) {
    return NULL;
  }

  return mg_date_time_zone_id_make(seconds_since_epoch, subseconds, timezone_name_str);
}

int is_datetime_timezone(PyObject *tzinfo) {
  if (tzinfo == Py_None) {
    return 0;
  }

  SCOPED_CLEANUP PyObject *datetime_module = PyImport_ImportModule("datetime");
  IF_PTR_IS_NULL_RETURN(datetime_module, 0);

  SCOPED_CLEANUP PyObject *timezone_class = PyObject_GetAttrString(datetime_module, "timezone");
  IF_PTR_IS_NULL_RETURN(timezone_class, 0);

  return PyObject_IsInstance(tzinfo, timezone_class);
}

mg_duration *py_delta_to_mg_duration(PyObject *obj) {
  int64_t days = PyDateTime_DELTA_GET_DAYS(obj);
  int64_t seconds = PyDateTime_DELTA_GET_SECONDS(obj);
  int64_t microseconds = PyDateTime_DELTA_GET_MICROSECONDS(obj);
  return mg_duration_make(0, days, seconds, microseconds * 1000);
}

mg_value *py_object_to_mg_value(PyObject *object) {
  mg_value *ret = NULL;

  if (object == Py_None) {
    ret = mg_value_make_null();
  } else if (PyBool_Check(object)) {
    ret = mg_value_make_bool(object == Py_True);
  } else if (PyLong_Check(object)) {
    int64_t as_int64 = PyLong_AsLongLong(object);
    if (as_int64 == -1 && PyErr_Occurred()) {
      return NULL;
    }
    ret = mg_value_make_integer(as_int64);
  } else if (PyFloat_Check(object)) {
    double as_double = PyFloat_AsDouble(object);
    if (as_double == -1.0 && PyErr_Occurred()) {
      return NULL;
    }
    ret = mg_value_make_float(as_double);
  } else if (PyUnicode_Check(object)) {
    mg_string *str = py_unicode_to_mg_string(object);
    if (!str) {
      return NULL;
    }
    ret = mg_value_make_string2(str);
  } else if (PyList_Check(object)) {
    mg_list *list = py_list_to_mg_list(object);
    if (!list) {
      return NULL;
    }
    ret = mg_value_make_list(list);
  } else if (PyDict_Check(object)) {
    mg_map *map = py_dict_to_mg_map(object);
    if (!map) {
      return NULL;
    }
    ret = mg_value_make_map(map);
  } else if (PyDate_CheckExact(object)) {
    mg_date *date = py_date_to_mg_date(object);
    if (!date) {
      return NULL;
    }
    ret = mg_value_make_date(date);
  } else if (PyTime_CheckExact(object)) {
    mg_local_time *lt = py_time_to_mg_local_time(object);
    if (!lt) {
      return NULL;
    }
    ret = mg_value_make_local_time(lt);
  } else if (PyDateTime_CheckExact(object)) {
    PyObject *tzinfo = INTERNAL_PyDateTime_DATE_GET_TZINFO(object);
    if (tzinfo != Py_None) {
      // The `timezone` may either be an offset based `datetime.timezone`, or
      // some kind of instance of `tzinfo`. In the case of the former, we
      // will use the offset; and in the case of the latter, use the `str()`
      // name of the timezone.
      if (is_datetime_timezone(tzinfo)) {
        mg_date_time *dt = py_date_time_to_mg_date_time(object);
        if (!dt) {
          return NULL;
        }
        ret = mg_value_make_date_time(dt);
      } else {
        mg_date_time_zone_id *dt_zone_id = py_date_time_to_mg_date_time_zone_id(object);
        if (!dt_zone_id) {
          return NULL;
        }
        ret = mg_value_make_date_time_zone_id(dt_zone_id);
      }
    } else {
      mg_local_date_time *ldt = py_date_time_to_mg_local_date_time(object);
      if (!ldt) {
        return NULL;
      }
      ret = mg_value_make_local_date_time(ldt);
    }
  } else if (PyDelta_CheckExact(object)) {
    mg_duration *dur = py_delta_to_mg_duration(object);
    if (!dur) {
      return NULL;
    }
    ret = mg_value_make_duration(dur);
  } else {
    PyErr_Format(PyExc_ValueError,
                 "value of type '%s' can't be used as query parameter",
                 Py_TYPE(object)->tp_name);
    return NULL;
  }

  if (!ret) {
    PyErr_SetString(PyExc_RuntimeError, "failed to create a mg_value");
    return NULL;
  }

  return ret;
}
