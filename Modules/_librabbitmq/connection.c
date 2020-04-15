#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include <sys/time.h>

#include <amqp.h>
#include <amqp_tcp_socket.h>

#include "connection.h"
#include "distmeta.h"
#include "_amqstate.h"

#define PYRABBITMQ_CONNECTION_ERROR 0x10
#define PYRABBITMQ_CHANNEL_ERROR 0x20

#define PYRABBITMQ_MODULE_NAME "_librabbitmq"
#define PYRABBITMQ_MODULE_DESC "Hand-made wrapper for librabbitmq."

/* ------: Private Prototypes :------------------------------------------- */
PyMODINIT_FUNC init_librabbitmq(void);

extern PyObject *PyRabbitMQ_socket_timeout;

/* Exceptions */
PyObject *PyRabbitMQExc_ConnectionError;
PyObject *PyRabbitMQExc_ChannelError;
PyObject *PyRabbitMQ_socket_timeout;


_PYRMQ_INLINE amqp_table_entry_t*
AMQTable_AddEntry(amqp_table_t*, amqp_bytes_t);

_PYRMQ_INLINE void
AMQTable_SetTableValue(amqp_table_t*, amqp_bytes_t, amqp_table_t);

_PYRMQ_INLINE void
AMQTable_SetArrayValue(amqp_table_t*, amqp_bytes_t, amqp_array_t);

_PYRMQ_INLINE void
AMQTable_SetStringValue(amqp_table_t*, amqp_bytes_t, amqp_bytes_t);

_PYRMQ_INLINE void
AMQTable_SetBoolValue(amqp_table_t*, amqp_bytes_t, int);

_PYRMQ_INLINE void
AMQTable_SetIntValue(amqp_table_t *, amqp_bytes_t, int);

_PYRMQ_INLINE void
AMQTable_SetNilValue(amqp_table_t *, amqp_bytes_t);

_PYRMQ_INLINE void
AMQTable_SetDoubleValue(amqp_table_t *, amqp_bytes_t, double);

_PYRMQ_INLINE amqp_field_value_t*
AMQArray_AddEntry(amqp_array_t*);

_PYRMQ_INLINE void
AMQArray_SetNilValue(amqp_array_t*);

_PYRMQ_INLINE void
AMQArray_SetTableValue(amqp_array_t*, amqp_table_t);

_PYRMQ_INLINE void
AMQArray_SetArrayValue(amqp_array_t*, amqp_array_t);

_PYRMQ_INLINE void
AMQArray_SetStringValue(amqp_array_t*, amqp_bytes_t);

_PYRMQ_INLINE void
AMQArray_SetIntValue(amqp_array_t *, int);

_PYRMQ_INLINE int64_t RabbitMQ_now_usec(void);
_PYRMQ_INLINE int RabbitMQ_wait_nb(int);
_PYRMQ_INLINE int RabbitMQ_wait_timeout(int, double);

_PYRMQ_INLINE void
basic_properties_to_PyDict(amqp_basic_properties_t*, PyObject*);

_PYRMQ_INLINE int
PyDict_to_basic_properties(PyObject *,
                           amqp_basic_properties_t *,
                           amqp_connection_state_t,
                           amqp_pool_t *,
                           pyobject_pool_t *);

_PYRMQ_INLINE void
amqp_basic_deliver_to_PyDict(PyObject *, uint64_t, amqp_bytes_t,
                             amqp_bytes_t, amqp_boolean_t);

_PYRMQ_INLINE int
PyRabbitMQ_ApplyCallback(PyRabbitMQ_Connection *,
                         PyObject *, PyObject *, PyObject *,
                         PyObject *, PyObject *);

int
PyRabbitMQ_recv(PyRabbitMQ_Connection *, PyObject *,
                amqp_connection_state_t, int);

unsigned int
PyRabbitMQ_revive_channel(PyRabbitMQ_Connection *, unsigned int);

    unsigned int
PyRabbitMQ_Connection_create_channel(PyRabbitMQ_Connection *, unsigned int);

int PyRabbitMQ_HandleError(int, char const *);
_PYRMQ_INLINE int PyRabbitMQ_HandlePollError(int);
int PyRabbitMQ_HandleAMQError(PyRabbitMQ_Connection *, unsigned int,
                              amqp_rpc_reply_t, const char *);
void PyRabbitMQ_SetErr_UnexpectedHeader(amqp_frame_t*);
int PyRabbitMQ_Not_Connected(PyRabbitMQ_Connection *);

static amqp_table_t PyDict_ToAMQTable(amqp_connection_state_t, PyObject *, amqp_pool_t *, pyobject_pool_t *);
static amqp_array_t PyIter_ToAMQArray(amqp_connection_state_t, PyObject *, amqp_pool_t *, pyobject_pool_t *);

static PyObject* AMQTable_toPyDict(amqp_table_t *table);
static PyObject* AMQArray_toPyList(amqp_array_t *array);

int PyRabbitMQ_HandleAMQStatus(int, const char *);

int PyRabbitMQ_Not_Connected(PyRabbitMQ_Connection *self)
{
    if (!self->connected) {
        PyErr_SetString(PyRabbitMQExc_ConnectionError,
            "Operation on closed connection");
        return 1;
    }
    return 0;
}

// Keep track of PyObject references with increased reference count
// Entries are stored in fixed size array.
#define PYOBJECT_ARRAY_MAX 5
typedef struct pyobject_array_t {
    int num_entries;
    PyObject *entries[PYOBJECT_ARRAY_MAX];
} pyobject_array_t;


static pyobject_pool_t *PyObjectPool_New(amqp_pool_t *);
static PyObject *PyObjectPool_AddEntry(pyobject_pool_t *, PyObject *);
static void PyObjectPool_XDECREF(pyobject_pool_t *array);

_PYRMQ_INLINE PyObject* PyObjectPool_Maybe_Unicode(PyObject *, pyobject_pool_t *);

static void PyObjectArray_XDECREF(pyobject_array_t *array);

_PYRMQ_INLINE PyObject* PyObjectArray_AddEntry(pyobject_array_t *, PyObject *obj);
_PYRMQ_INLINE PyObject* PyObjectArray_Maybe_Unicode(PyObject *, pyobject_array_t *);

/* ------: AMQP Utils :--------------------------------------------------- */

_PYRMQ_INLINE amqp_table_entry_t*
AMQTable_AddEntry(amqp_table_t *table, amqp_bytes_t key)
{
    amqp_table_entry_t *entry = &table->entries[table->num_entries];
    table->num_entries++;
    entry->key = key;
    return entry;
}

_PYRMQ_INLINE void
AMQTable_SetTableValue(amqp_table_t *table,
                       amqp_bytes_t key, amqp_table_t value)
{
    amqp_table_entry_t *entry = AMQTable_AddEntry(table, key);
    entry->value.kind = AMQP_FIELD_KIND_TABLE;
    entry->value.value.table = value;
}

_PYRMQ_INLINE void
AMQTable_SetArrayValue(amqp_table_t *table,
                       amqp_bytes_t key, amqp_array_t value)
{
    amqp_table_entry_t *entry = AMQTable_AddEntry(table, key);
    entry->value.kind = AMQP_FIELD_KIND_ARRAY;
    entry->value.value.array = value;
}

_PYRMQ_INLINE void
AMQTable_SetStringValue(amqp_table_t *table,
                        amqp_bytes_t key, amqp_bytes_t value)
{
    amqp_table_entry_t *entry = AMQTable_AddEntry(table, key);
    entry->value.kind = AMQP_FIELD_KIND_UTF8;
    entry->value.value.bytes = value;
}

_PYRMQ_INLINE void
AMQTable_SetBoolValue(amqp_table_t *table,
                      amqp_bytes_t key, int value)
{
    amqp_table_entry_t *entry = AMQTable_AddEntry(table, key);
    entry->value.kind = AMQP_FIELD_KIND_BOOLEAN;
    entry->value.value.boolean = value;
}

_PYRMQ_INLINE void
AMQTable_SetIntValue(amqp_table_t *table,
                     amqp_bytes_t key, int value)
{
    amqp_table_entry_t *entry = AMQTable_AddEntry(table, key);
    entry->value.kind = AMQP_FIELD_KIND_I32;
    entry->value.value.i32 = value;
}

_PYRMQ_INLINE void
AMQTable_SetNilValue(amqp_table_t *table, amqp_bytes_t key) {
    amqp_table_entry_t *entry = AMQTable_AddEntry(table, key);
    entry->value.kind = AMQP_FIELD_KIND_VOID;
}

_PYRMQ_INLINE void
AMQTable_SetDoubleValue(amqp_table_t *table,
                        amqp_bytes_t key, double value)
{
    amqp_table_entry_t *entry = AMQTable_AddEntry(table, key);
    entry->value.kind = AMQP_FIELD_KIND_F64;
    entry->value.value.f64 = value;
}

_PYRMQ_INLINE amqp_field_value_t*
AMQArray_AddEntry(amqp_array_t *array)
{
    amqp_field_value_t *entry = &array->entries[array->num_entries];
    array->num_entries++;
    return entry;
}

_PYRMQ_INLINE void
AMQArray_SetNilValue(amqp_array_t *array)
{
    amqp_field_value_t *entry = AMQArray_AddEntry(array);
    entry->kind = AMQP_FIELD_KIND_VOID;
}

_PYRMQ_INLINE void
AMQArray_SetTableValue(amqp_array_t *array, amqp_table_t value)
{
    amqp_field_value_t *entry = AMQArray_AddEntry(array);
    entry->kind = AMQP_FIELD_KIND_TABLE;
    entry->value.table = value;
}

_PYRMQ_INLINE void
AMQArray_SetArrayValue(amqp_array_t *array, amqp_array_t value)
{
    amqp_field_value_t *entry = AMQArray_AddEntry(array);
    entry->kind = AMQP_FIELD_KIND_ARRAY;
    entry->value.array = value;
}

_PYRMQ_INLINE void
AMQArray_SetStringValue(amqp_array_t *array, amqp_bytes_t value)
{
    amqp_field_value_t *entry = AMQArray_AddEntry(array);
    entry->kind = AMQP_FIELD_KIND_UTF8;
    entry->value.bytes = value;
}

_PYRMQ_INLINE void
AMQArray_SetIntValue(amqp_array_t *array, int value)
{
    amqp_field_value_t *entry = AMQArray_AddEntry(array);
    entry->kind = AMQP_FIELD_KIND_I32;
    entry->value.i32 = value;
}

static amqp_table_t
PyDict_ToAMQTable(amqp_connection_state_t conn, PyObject *src, amqp_pool_t *pool, pyobject_pool_t *pyobj_pool)
{
    PyObject *dkey = NULL;
    PyObject *dvalue = NULL;
    Py_ssize_t size = 0;
    Py_ssize_t pos = 0;
    uint64_t clong_value = 0;
    double cdouble_value = 0.0;
    int is_unicode = 0;
    amqp_table_t dst = amqp_empty_table;

    size = PyDict_Size(src);

    /* allocate new table */
    dst.num_entries = 0;
    dst.entries = amqp_pool_alloc(pool, size * sizeof(amqp_table_entry_t));
    while (PyDict_Next(src, &pos, &dkey, &dvalue)) {
        dkey = PyObjectPool_Maybe_Unicode(dkey, pyobj_pool);

        if (dvalue == Py_None) {
            /* None */
            AMQTable_SetNilValue(&dst, PyString_AS_AMQBYTES(dkey));
        }
        else if (PyDict_Check(dvalue)) {
            /* Dict */
            AMQTable_SetTableValue(&dst,
                    PyString_AS_AMQBYTES(dkey),
                    PyDict_ToAMQTable(conn, dvalue, pool, pyobj_pool));
        }
        else if (PyList_Check(dvalue) || PyTuple_Check(dvalue)) {
            /* List */
            AMQTable_SetArrayValue(&dst,
                    PyString_AS_AMQBYTES(dkey),
                    PyIter_ToAMQArray(conn, dvalue, pool, pyobj_pool));
        }
        else if (PyBool_Check(dvalue)) {
          /* Bool */
          clong_value = 0;  /* default false */

          if (dvalue == Py_True)
            clong_value = 1;

          AMQTable_SetBoolValue(&dst,
                                PyString_AS_AMQBYTES(dkey),
                                clong_value);
        }
        else if (PyLong_Check(dvalue) || PyInt_Check(dvalue)) {
            /* Int | Long */
            clong_value = (int64_t)PyLong_AsLong(dvalue);

            if (clong_value == -1)
              goto error;

            AMQTable_SetIntValue(&dst,
                    PyString_AS_AMQBYTES(dkey),
                    clong_value
            );
        }
        else if (PyFloat_Check(dvalue)) {
            cdouble_value = PyFloat_AsDouble(dvalue);

            if (cdouble_value == -1)
              goto error;

            AMQTable_SetDoubleValue(&dst,
                PyString_AS_AMQBYTES(dkey),
                    cdouble_value
            );
        }
        else {
            /* String | Unicode */
            is_unicode = PyUnicode_Check(dvalue);
            if (is_unicode || PyBytes_Check(dvalue)) {
                if (is_unicode) {
                    if ((dvalue = PyUnicode_AsEncodedString(dvalue, "utf-8", "strict")) == NULL)
                        goto error;
                    PyObjectPool_AddEntry(pyobj_pool, dvalue);
                }
                AMQTable_SetStringValue(&dst,
                        PyString_AS_AMQBYTES(dkey),
                        PyString_AS_AMQBYTES(dvalue)
                );
            }
            else {
                /* unsupported type */
                PyErr_Format(PyExc_ValueError,
                    "Table member %s is of an unsupported type",
                    PyBytes_AS_STRING(dkey));
                goto error;
            }
        }
    }
    return dst;
error:
    assert(PyErr_Occurred());
    return amqp_empty_table;
}

static amqp_array_t
PyIter_ToAMQArray(amqp_connection_state_t conn, PyObject *src, amqp_pool_t *pool, pyobject_pool_t *pyobj_pool)
{
    Py_ssize_t pos = 0;
    uint64_t clong_value = 0;
    int is_unicode = 0;
    amqp_array_t dst = amqp_empty_array;

    Py_ssize_t size = PySequence_Size(src);
    if (size == -1) return dst;

    PyObject *iterator = PyObject_GetIter(src);
    if (iterator == NULL) return dst;
    PyObject *item = NULL;
    PyObject *item_tmp = NULL;

    /* allocate new amqp array */
    dst.num_entries = 0;
    dst.entries = amqp_pool_alloc(pool, size * sizeof(amqp_field_value_t));

    while (item = PyIter_Next(iterator)) {
        if (item == Py_None) {
            /* None */
            AMQArray_SetNilValue(&dst);
        }
        else if (PyDict_Check(item)) {
            /* Dict */
            AMQArray_SetTableValue(
                &dst, PyDict_ToAMQTable(conn, item, pool, pyobj_pool));
        }
        else if (PyList_Check(item) || PyTuple_Check(item)) {
            /* List */
            AMQArray_SetArrayValue(
                &dst, PyIter_ToAMQArray(conn, item, pool, pyobj_pool));
        }
        else if (PyLong_Check(item) || PyInt_Check(item)) {
            /* Int | Long */
            clong_value = (int64_t)PyLong_AsLongLong(item);
            AMQArray_SetIntValue(&dst, clong_value);
        }
        else {
            /* String | Unicode */
            is_unicode = PyUnicode_Check(item);
            if (is_unicode || PyBytes_Check(item)) {
                if (is_unicode) {
                    /* PyUnicode_AsASCIIString returns a new ref! */
                    item_tmp = item;
                    if ((item = PyUnicode_AsASCIIString(item)) == NULL)
                        goto item_error;
                    Py_XDECREF(item_tmp);
                }
                AMQArray_SetStringValue(
                    &dst, PyString_AS_AMQBYTES(item));
            }
            else {
                /* unsupported type */
                PyErr_Format(PyExc_ValueError,
                    "Array member at index %lu, %R, is of an unsupported type",
                    pos, item);
                goto item_error;
            }
        }
        Py_XDECREF(item);
    }

    return dst;
item_error:
    Py_XDECREF(item_tmp);
    Py_XDECREF(item);
error:
    Py_XDECREF(iterator);
    assert(PyErr_Occurred());
    return dst;
}


_PYRMQ_INLINE int64_t RabbitMQ_now_usec(void)
{
   struct timeval tv;
   gettimeofday(&tv, NULL);
   return (int64_t)tv.tv_sec * 10e5 + (int64_t)tv.tv_usec;
}

_PYRMQ_INLINE int RabbitMQ_wait_nb(int sockfd)
{
    register int result = 0;
    fd_set fdset;
    struct timeval tv = {0, 0};

    FD_ZERO(&fdset);
    FD_SET(sockfd, &fdset);

    result = select(sockfd + 1, &fdset, NULL, NULL, &tv);
    if (result <= 0)
        return result;
    if (FD_ISSET(sockfd, &fdset))
        return 1;
    return 0;
}

_PYRMQ_INLINE int RabbitMQ_wait_timeout(int sockfd, double timeout)
{
    int64_t t1, t2;
    register int result = 0;
    fd_set fdset;
    struct timeval tv;

    while (timeout > 0.0) {
        FD_ZERO(&fdset);
        FD_SET(sockfd, &fdset);
        tv.tv_sec = (int)timeout;
        tv.tv_usec = (int)((timeout - tv.tv_sec) * 1e6);

        t1 = RabbitMQ_now_usec();
        result = select(sockfd + 1, &fdset, NULL, NULL, &tv);

        if (result <= 0)
            break;

        if (FD_ISSET(sockfd, &fdset)) {
            result = 1;
            break;
        }

        t2 = RabbitMQ_now_usec();
        timeout -= (double)(t2 / 1e6) - (t1 / 1e6);
    }
    return result;
}


_PYRMQ_INLINE void
basic_properties_to_PyDict(amqp_basic_properties_t *props, PyObject *p)
{
    register PyObject *value = NULL;

    if (props->_flags & AMQP_BASIC_CONTENT_TYPE_FLAG) {
        value = PySTRING_FROM_AMQBYTES(props->content_type);
        PyDICT_SETSTR_DECREF(p, "content_type", value);
    }
    if (props->_flags & AMQP_BASIC_CONTENT_ENCODING_FLAG) {
        value = PySTRING_FROM_AMQBYTES(props->content_encoding);
        PyDICT_SETSTR_DECREF(p, "content_encoding", value);
    }
    if (props->_flags & AMQP_BASIC_CORRELATION_ID_FLAG) {
        value = PySTRING_FROM_AMQBYTES(props->correlation_id);
        PyDICT_SETSTR_DECREF(p, "correlation_id", value);
    }
    if (props->_flags & AMQP_BASIC_REPLY_TO_FLAG) {
        value = PySTRING_FROM_AMQBYTES(props->reply_to);
        PyDICT_SETSTR_DECREF(p, "reply_to", value);
    }
    if (props->_flags & AMQP_BASIC_EXPIRATION_FLAG) {
        value = PySTRING_FROM_AMQBYTES(props->expiration);
        PyDICT_SETSTR_DECREF(p, "expiration", value);
    }
    if (props->_flags & AMQP_BASIC_MESSAGE_ID_FLAG) {
        value = PySTRING_FROM_AMQBYTES(props->message_id);
        PyDICT_SETSTR_DECREF(p, "message_id", value);
    }
    if (props->_flags & AMQP_BASIC_TYPE_FLAG) {
        value = PySTRING_FROM_AMQBYTES(props->type);
        PyDICT_SETSTR_DECREF(p, "type", value);
    }
    if (props->_flags & AMQP_BASIC_USER_ID_FLAG) {
        value = PySTRING_FROM_AMQBYTES(props->user_id);
        PyDICT_SETSTR_DECREF(p, "user_id", value);
    }
    if (props->_flags & AMQP_BASIC_APP_ID_FLAG) {
        value = PySTRING_FROM_AMQBYTES(props->app_id);
        PyDICT_SETSTR_DECREF(p, "app_id", value);
    }
    if (props->_flags & AMQP_BASIC_DELIVERY_MODE_FLAG) {
        value = PyInt_FromLong(props->delivery_mode);
        PyDICT_SETSTR_DECREF(p, "delivery_mode", value);
    }
    if (props->_flags & AMQP_BASIC_PRIORITY_FLAG) {
        value =PyInt_FromLong(props->priority);
        PyDICT_SETSTR_DECREF(p, "priority", value);
    }
    if (props->_flags & AMQP_BASIC_TIMESTAMP_FLAG) {
        value = PyInt_FromLong(props->timestamp);
        PyDICT_SETSTR_DECREF(p, "timestamp", value);
    }
    if (props->_flags & AMQP_BASIC_HEADERS_FLAG) {
        value = AMQTable_toPyDict(&(props->headers));
        PyDICT_SETSTR_DECREF(p, "headers", value);
    }
}


static PyObject*
AMQTable_toPyDict(amqp_table_t *table)
{
    register PyObject *key = NULL;
    register PyObject *value = NULL;
    PyObject *dict = NULL;
    dict = PyDict_New();

    if (table) {
        int i;
        for (i = 0; i < table->num_entries; ++i, key=value=NULL) {
            switch (table->entries[i].value.kind) {
                case AMQP_FIELD_KIND_VOID:
                    value = Py_None;
                    break;
                case AMQP_FIELD_KIND_BOOLEAN:
                    value = PyBool_FromLong(AMQTable_VAL(table, i, boolean));
                    break;
                case AMQP_FIELD_KIND_I8:
                    value = PyInt_FromLong(AMQTable_VAL(table, i, i8));
                    break;
                case AMQP_FIELD_KIND_I16:
                    value = PyInt_FromLong(AMQTable_VAL(table, i, i16));
                    break;
                case AMQP_FIELD_KIND_I32:
                    value = PyInt_FromLong(AMQTable_VAL(table, i, i32));
                    break;
                case AMQP_FIELD_KIND_I64:
                    value = PyLong_FromLong(AMQTable_VAL(table, i, i64));
                    break;
                case AMQP_FIELD_KIND_U8:
                    value = PyLong_FromUnsignedLong(
                            AMQTable_VAL(table, i, u8));
                    break;
                case AMQP_FIELD_KIND_U16:
                    value = PyLong_FromUnsignedLong(
                            AMQTable_VAL(table, i, u16));
                    break;
                case AMQP_FIELD_KIND_U32:
                    value = PyLong_FromUnsignedLong(
                            AMQTable_VAL(table, i, u32));
                    break;
                case AMQP_FIELD_KIND_U64:
                    value = PyLong_FromUnsignedLong(
                            AMQTable_VAL(table, i, u64));
                    break;
                case AMQP_FIELD_KIND_F32:
                    value = PyFloat_FromDouble(AMQTable_VAL(table, i, f32));
                    break;
                case AMQP_FIELD_KIND_F64:
                    value = PyFloat_FromDouble(AMQTable_VAL(table, i, f64));
                    break;
                case AMQP_FIELD_KIND_UTF8:
                    value = PySTRING_FROM_AMQBYTES(
                            AMQTable_VAL(table, i, bytes));
                    break;
                case AMQP_FIELD_KIND_TABLE:
                    value = AMQTable_toPyDict(&(AMQTable_VAL(table, i, table)));
                    break;
                case AMQP_FIELD_KIND_ARRAY:
                    value = AMQArray_toPyList(&(AMQTable_VAL(table, i, array)));
                    break;
            }

            key = AMQTable_TO_PYKEY(table, i);
            if (value == Py_None || value == NULL)
                /* unsupported type */
                PyDICT_SETNONE_DECREF(dict, key);
            else
                PyDICT_SETKV_DECREF(dict, key, value);

        }
    }
    return dict;
}


static PyObject*
AMQArray_toPyList(amqp_array_t *array)
{
    register PyObject *value = NULL;
    PyObject *list = NULL;
    list = PyList_New((Py_ssize_t) array->num_entries);

    if (array) {
        int i;
        for (i = 0; i < array->num_entries; ++i, value=NULL) {
            switch (array->entries[i].kind) {
                case AMQP_FIELD_KIND_BOOLEAN:
                    value = PyBool_FromLong(AMQArray_VAL(array, i, boolean));
                    break;
                case AMQP_FIELD_KIND_I8:
                    value = PyInt_FromLong(AMQArray_VAL(array, i, i8));
                    break;
                case AMQP_FIELD_KIND_I16:
                    value = PyInt_FromLong(AMQArray_VAL(array, i, i16));
                    break;
                case AMQP_FIELD_KIND_I32:
                    value = PyInt_FromLong(AMQArray_VAL(array, i, i32));
                    break;
                case AMQP_FIELD_KIND_I64:
                    value = PyLong_FromLong(AMQArray_VAL(array, i, i64));
                    break;
                case AMQP_FIELD_KIND_U8:
                    value = PyLong_FromUnsignedLong(AMQArray_VAL(array, i, u8));
                    break;
                case AMQP_FIELD_KIND_U16:
                    value = PyLong_FromUnsignedLong(
                            AMQArray_VAL(array, i, u16));
                    break;
                case AMQP_FIELD_KIND_U32:
                    value = PyLong_FromUnsignedLong(
                            AMQArray_VAL(array, i, u32));
                    break;
                case AMQP_FIELD_KIND_U64:
                    value = PyLong_FromUnsignedLong(
                            AMQArray_VAL(array, i, u64));
                    break;
                case AMQP_FIELD_KIND_F32:
                    value = PyFloat_FromDouble(AMQArray_VAL(array, i, f32));
                    break;
                case AMQP_FIELD_KIND_F64:
                    value = PyFloat_FromDouble(AMQArray_VAL(array, i, f64));
                    break;
                case AMQP_FIELD_KIND_UTF8:
                    value = PySTRING_FROM_AMQBYTES(
                            AMQArray_VAL(array, i, bytes));
                    break;
                case AMQP_FIELD_KIND_TABLE:
                    value = AMQTable_toPyDict(&(AMQArray_VAL(array, i, table)));
                    break;
                case AMQP_FIELD_KIND_ARRAY:
                    value = AMQArray_toPyList(&(AMQArray_VAL(array, i, array)));
                    break;
                default:
                    /* unsupported type */
                    Py_INCREF(Py_None);
                    value = Py_None;
                    break;
            }

            PyList_SET_ITEM(list, i, value);

        }
    }
    return list;
}

_PYRMQ_INLINE int
PyDict_to_basic_properties(PyObject *p,
                           amqp_basic_properties_t *props,
                           amqp_connection_state_t conn,
                           amqp_pool_t *pool,
                           pyobject_pool_t *pyobj_pool)
{
    PyObject *value = NULL;
    props->headers = amqp_empty_table;
    props->_flags = AMQP_BASIC_HEADERS_FLAG;

    if ((value = PyDict_GetItemString(p, "content_type")) != NULL) {
        if ((value = PyObjectPool_Maybe_Unicode(value, pyobj_pool)) == NULL) return -1;
        props->content_type = PyString_AS_AMQBYTES(value);
        props->_flags |= AMQP_BASIC_CONTENT_TYPE_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "content_encoding")) != NULL) {
        if ((value = PyObjectPool_Maybe_Unicode(value, pyobj_pool)) == NULL) return -1;
        props->content_encoding = PyString_AS_AMQBYTES(value);
        props->_flags |= AMQP_BASIC_CONTENT_ENCODING_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "correlation_id")) != NULL) {
        if ((value = PyObjectPool_Maybe_Unicode(value, pyobj_pool)) == NULL) return -1;
        props->correlation_id = PyString_AS_AMQBYTES(value);
        props->_flags |= AMQP_BASIC_CORRELATION_ID_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "reply_to")) != NULL) {
        if ((value = PyObjectPool_Maybe_Unicode(value, pyobj_pool)) == NULL) return -1;
        props->reply_to = PyString_AS_AMQBYTES(value);
        props->_flags |= AMQP_BASIC_REPLY_TO_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "expiration")) != NULL) {
        if ((value = PyObjectPool_Maybe_Unicode(value, pyobj_pool)) == NULL) return -1;
        props->expiration = PyString_AS_AMQBYTES(value);
        props->_flags |= AMQP_BASIC_EXPIRATION_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "message_id")) != NULL) {
        if ((value = PyObjectPool_Maybe_Unicode(value, pyobj_pool)) == NULL) return -1;
        props->message_id = PyString_AS_AMQBYTES(value);
        props->_flags |= AMQP_BASIC_MESSAGE_ID_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "type")) != NULL) {
        if ((value = PyObjectPool_Maybe_Unicode(value, pyobj_pool)) == NULL) return -1;
        props->type = PyString_AS_AMQBYTES(value);
        props->_flags |= AMQP_BASIC_TYPE_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "user_id")) != NULL) {
        if ((value = PyObjectPool_Maybe_Unicode(value, pyobj_pool)) == NULL) return -1;
        props->user_id = PyString_AS_AMQBYTES(value);
        props->_flags |= AMQP_BASIC_USER_ID_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "app_id")) != NULL) {
        if ((value = PyObjectPool_Maybe_Unicode(value, pyobj_pool)) == NULL) return -1;
        props->app_id = PyString_AS_AMQBYTES(value);
        props->_flags |= AMQP_BASIC_APP_ID_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "delivery_mode")) != NULL) {
        props->delivery_mode = (uint8_t)PyInt_AS_LONG(value);
        props->_flags |= AMQP_BASIC_DELIVERY_MODE_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "priority")) != NULL) {
        props->priority = (uint8_t)PyInt_AS_LONG(value);
        props->_flags |= AMQP_BASIC_PRIORITY_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "timestamp")) != NULL) {
        props->timestamp = (uint64_t)PyInt_AS_LONG(value);
        props->_flags |= AMQP_BASIC_TIMESTAMP_FLAG;
    }

    if ((value = PyDict_GetItemString(p, "headers")) != NULL) {
        props->headers = PyDict_ToAMQTable(conn, value, pool, pyobj_pool);
        if (PyErr_Occurred()) return -1;
    }
    return 1;
}

_PYRMQ_INLINE void
amqp_basic_deliver_to_PyDict(PyObject *dest,
        uint64_t delivery_tag,
        amqp_bytes_t exchange,
        amqp_bytes_t routing_key,
        amqp_boolean_t redelivered)
{
    PyObject *value = NULL;

    /* -- delivery_tag (PyInt)                               */
    value = PyLong_FromLongLong(delivery_tag);
    PyDICT_SETSTR_DECREF(dest, "delivery_tag", value);

    /* -- exchange (PyString)                                */
    value = PySTRING_FROM_AMQBYTES(exchange);
    PyDICT_SETSTR_DECREF(dest, "exchange", value);

    /* -- routing_key (PyString)                             */
    value = PySTRING_FROM_AMQBYTES(routing_key);
    PyDICT_SETSTR_DECREF(dest, "routing_key", value);

    /* -- redelivered (PyBool)                               */
    value = PyBool_FromLong((long)redelivered);
    PyDICT_SETSTR_DECREF(dest, "redelivered", value);

    return;
}

/* ------: Keep track of increased reference counts :------------------------ */

_PYRMQ_INLINE PyObject*
PyObjectArray_Maybe_Unicode(PyObject *s, pyobject_array_t *pyobj_pool)
{
    if (PyUnicode_Check(s)) {
        return PyObjectArray_AddEntry(pyobj_pool, PyUnicode_AsASCIIString(s));
    }
    return s;
}

_PYRMQ_INLINE PyObject*
PyObjectPool_Maybe_Unicode(PyObject *s, pyobject_pool_t *pyobj_pool)
{
    if (PyUnicode_Check(s)) {
        return PyObjectPool_AddEntry(pyobj_pool, PyUnicode_AsASCIIString(s));
    }
    return s;
}

_PYRMQ_INLINE PyObject*
PyObjectArray_AddEntry(pyobject_array_t *array, PyObject *obj)
{
    if (obj && array->num_entries < PYOBJECT_ARRAY_MAX) {
        array->entries[array->num_entries] = obj;
        array->num_entries++;
    }

    return obj;
}

static PyObject *PyObjectPool_AddEntry(pyobject_pool_t *array, PyObject *obj)
{
    if (obj) {
        if (array->num_entries == PYOBJECT_POOL_MAX) {
            if (!array->next)
                array->next = PyObjectPool_New(array->pool);

            PyObjectPool_AddEntry(array->next, obj);
        } else {
            array->entries[array->num_entries] = obj;
            array->num_entries++;
        }
    }

    return obj;
}

static pyobject_pool_t *PyObjectPool_New(amqp_pool_t *pool)
{
    pyobject_pool_t *array = amqp_pool_alloc(pool, sizeof(pyobject_pool_t));
    array->num_entries = 0;
    array->entries = amqp_pool_alloc(
        pool, PYOBJECT_POOL_MAX * sizeof(PyObject *));
    array->pool = pool;
    array->next = (pyobject_pool_t *) 0;

    return array;
}

static void PyObjectPool_XDECREF(pyobject_pool_t *array)
{
    int i;

    if (!array)
        return;

    for (i = 0; i < array->num_entries; ++i) {
        Py_XDECREF(array->entries[i]);
    }

    array->num_entries = 0;

    if (array->next) {
        PyObjectPool_XDECREF(array->next);
    }
}

static void PyObjectArray_XDECREF(pyobject_array_t *array)
{
    int i;

    for (i = 0; i < array->num_entries; ++i) {
        Py_XDECREF(array->entries[i]);
    }

    array->num_entries = 0;
}

/* ------: Error Handlers :----------------------------------------------- */

int PyRabbitMQ_HandleError(int ret, char const *context)
{
    if (ret < 0) {
        char errorstr[1024];
        snprintf(errorstr, sizeof(errorstr), "%s: %s",
                context, strerror(-ret));
        PyErr_SetString(PyRabbitMQExc_ConnectionError, errorstr);
        return 0;
    }
    return 1;
}


_PYRMQ_INLINE int
PyRabbitMQ_HandlePollError(int ready)
{
    if (ready < 0 && !PyErr_Occurred())
        PyErr_SetFromErrno(PyExc_OSError);
    if (!ready && !PyErr_Occurred())
        PyErr_SetString(PyRabbitMQ_socket_timeout, "timed out");
    return ready;
}


int PyRabbitMQ_HandleAMQStatus(int status, const char *context)
{
    char errorstr[1024];
    if (status) {
        snprintf(errorstr, sizeof(errorstr), "%s: %s",
                context, amqp_error_string2(status));
        PyErr_SetString(PyRabbitMQExc_ConnectionError, errorstr);
    }
    return status;
}


int PyRabbitMQ_HandleAMQError(PyRabbitMQ_Connection *self, unsigned int channel,
        amqp_rpc_reply_t reply, const char *context)
{
    char errorstr[1024];

    switch (reply.reply_type) {
        case AMQP_RESPONSE_NORMAL:
            return 0;

        case AMQP_RESPONSE_NONE:
            snprintf(errorstr, sizeof(errorstr),
                    "%s: missing RPC reply type!", context);
            goto connerror;

        case AMQP_RESPONSE_LIBRARY_EXCEPTION:
            snprintf(errorstr, sizeof(errorstr), "%s: %s",
                    context,
                    reply.library_error
                        ? amqp_error_string2(reply.library_error)
                        : "(end-of-stream)");
            goto connerror;

        case AMQP_RESPONSE_SERVER_EXCEPTION:

            switch (reply.reply.id) {
                case AMQP_CONNECTION_CLOSE_METHOD: {
                    amqp_connection_close_t *m = (amqp_connection_close_t *) reply.reply.decoded;
                    snprintf(errorstr, sizeof(errorstr),
                        "%s: server connection error %d, message: %.*s",
                            context,
                            m->reply_code,
                            (int) m->reply_text.len, (char *) m->reply_text.bytes);
                    goto connerror;
                }
                case AMQP_CHANNEL_CLOSE_METHOD: {
                    amqp_channel_close_t *m = (amqp_channel_close_t *) reply.reply.decoded;
                    snprintf(errorstr, sizeof(errorstr),
                        "%s: server channel error %d, message: %.*s",
                            context, m->reply_code,
                            (int) m->reply_text.len, (char *) m->reply_text.bytes);
                    goto chanerror;
                }
                default:
                    snprintf(errorstr, sizeof(errorstr),
                        "%s: unknown server error, method id 0x%08X",
                            context, reply.reply.id);
                    goto connerror;
            }
            break;
    }
connerror:
    PyErr_SetString(PyRabbitMQExc_ConnectionError, errorstr);
    PyRabbitMQ_Connection_close(self);
    return PYRABBITMQ_CONNECTION_ERROR;
chanerror:
    PyErr_SetString(PyRabbitMQExc_ChannelError, errorstr);
    PyRabbitMQ_revive_channel(self, channel);
    return PYRABBITMQ_CHANNEL_ERROR;
}


void PyRabbitMQ_SetErr_UnexpectedHeader(amqp_frame_t* frame)
{
    char errorstr[1024];
    snprintf(errorstr, sizeof(errorstr),
        "Unexpected header %d", frame->frame_type);
    PyErr_SetString(PyRabbitMQExc_ChannelError, errorstr);
}

unsigned int
PyRabbitMQ_revive_channel(PyRabbitMQ_Connection *self, unsigned int channel)
{
    int status = -1;
    amqp_channel_close_ok_t req;
    status = amqp_send_method(self->conn, (amqp_channel_t)channel,
                            AMQP_CHANNEL_CLOSE_OK_METHOD, &req);
    if (status < 0) {
        PyErr_SetString(PyRabbitMQExc_ConnectionError, "Couldn't revive channel");
        PyRabbitMQ_Connection_close(self);
        return 1;
    }
    return PyRabbitMQ_Connection_create_channel(self, channel);
}

/* ------: Connection :--------------------------------------------------- */

/*
 * Connection.__new__()
 * */
static PyRabbitMQ_Connection*
PyRabbitMQ_ConnectionType_new(PyTypeObject *type,
                              PyObject *args, PyObject *kwargs)
{
    PyRabbitMQ_Connection *self;

    self = (PyRabbitMQ_Connection *)PyType_GenericNew(type, args, kwargs);
    if (self != NULL) {
        self->conn = NULL;
        self->hostname = NULL;
        self->userid = NULL;
        self->password = NULL;
        self->virtual_host = NULL;
        self->port = 5672;
        self->sockfd = 0;
        self->connected = 0;
        self->server_properties = NULL;
        self->callbacks = NULL;
    }
    return self;
}


/*
 * Connection.__del__()
 */
static void
PyRabbitMQ_ConnectionType_dealloc(PyRabbitMQ_Connection *self)
{
    if (self->weakreflist != NULL)
        PyObject_ClearWeakRefs((PyObject*)self);

    if (self->hostname != NULL)
      PyMem_Free(self->hostname);

    if (self->userid != NULL)
      PyMem_Free(self->userid);

    if (self->password != NULL)
      PyMem_Free(self->password);

    if (self->virtual_host != NULL)
      PyMem_Free(self->virtual_host);

    Py_XDECREF(self->callbacks);
    Py_XDECREF(self->client_properties);
    Py_XDECREF(self->server_properties);
    Py_TYPE(self)->tp_free(self);
}


/*
 * Connection.__init__()
 */
static int
PyRabbitMQ_ConnectionType_init(PyRabbitMQ_Connection *self,
                               PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {
        "hostname",
        "userid",
        "password",
        "virtual_host",
        "port",
        "channel_max",
        "frame_max",
        "heartbeat",
        "client_properties",
        NULL
    };
    char *hostname;
    char *userid;
    char *password;
    char *virtual_host;

    int channel_max = 0xffff;
    int frame_max = 131072;
    int heartbeat = 0;
    int port = 5672;
    PyObject *client_properties = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|ssssiiiiO", kwlist,
                                     &hostname, &userid, &password, &virtual_host, &port,
                                     &channel_max, &frame_max, &heartbeat, &client_properties)) {
        return -1;
    }

    self->hostname = PyMem_Malloc(strlen(hostname) + 1);
    self->userid = PyMem_Malloc(strlen(userid) + 1);
    self->password = PyMem_Malloc(strlen(password) + 1);
    self->virtual_host = PyMem_Malloc(strlen(virtual_host) + 1);

    if (self->hostname == NULL || self->userid == NULL || self->password == NULL || self->virtual_host == NULL) {
        return PyErr_NoMemory();
    }

    strcpy(self->hostname, hostname);
    strcpy(self->userid, userid);
    strcpy(self->password, password);
    strcpy(self->virtual_host, virtual_host);

    self->port = port;
    self->channel_max = channel_max;
    self->frame_max = frame_max;
    self->heartbeat = heartbeat;
    self->weakreflist = NULL;
    self->callbacks = PyDict_New();
    if (self->callbacks == NULL) return -1;

    Py_XINCREF(client_properties);
    self->client_properties = client_properties;
    self->server_properties = NULL;
    return 0;
}


/*
 * Connection.fileno()
 */
static PyObject*
PyRabbitMQ_Connection_fileno(PyRabbitMQ_Connection *self)
{
    if (self->sockfd > 0) {
        return PyInt_FromLong((long)self->sockfd);
    }
    else {
        PyErr_SetString(PyExc_ValueError, "Socket not connected");
        return 0;
    }
}


/*
 * Connection.connect()
 */
static PyObject*
PyRabbitMQ_Connection_connect(PyRabbitMQ_Connection *self)
{
    int status;
    amqp_socket_t *socket = NULL;
    amqp_rpc_reply_t reply;
    amqp_pool_t pool;
    amqp_table_t properties;

    pyobject_pool_t *pyobj_pool = NULL;

    if (self->connected) {
        PyErr_SetString(PyRabbitMQExc_ConnectionError, "Already connected");
        goto bail;
    }
    Py_BEGIN_ALLOW_THREADS;
    self->conn = amqp_new_connection();
    socket = amqp_tcp_socket_new(self->conn);
    Py_END_ALLOW_THREADS;

    if (!socket) {
        PyErr_NoMemory();
        goto error;
    }
    Py_BEGIN_ALLOW_THREADS;
    status = amqp_socket_open(socket, self->hostname, self->port);
    Py_END_ALLOW_THREADS;
    if (PyRabbitMQ_HandleAMQStatus(status, "Error opening socket")) {
        goto error;
    }

    Py_BEGIN_ALLOW_THREADS;
    self->sockfd = amqp_socket_get_sockfd(socket);

    if (self->client_properties != NULL && PyDict_Check(self->client_properties)) {
      init_amqp_pool(&pool, self->frame_max);
      pyobj_pool = PyObjectPool_New(&pool);
      properties = PyDict_ToAMQTable(self->conn, self->client_properties, &pool, pyobj_pool);

      reply = amqp_login_with_properties(self->conn, self->virtual_host, self->channel_max,
                                         self->frame_max, self->heartbeat,
                                         &properties,
                                         AMQP_SASL_METHOD_PLAIN, self->userid, self->password);
      PyObjectPool_XDECREF(pyobj_pool);
    } else {
      reply = amqp_login(self->conn, self->virtual_host, self->channel_max,
                         self->frame_max, self->heartbeat,
                         AMQP_SASL_METHOD_PLAIN, self->userid, self->password);
    }

    Py_END_ALLOW_THREADS;

    if (PyRabbitMQ_HandleAMQError(self, 0, reply, "Couldn't log in"))
      goto bail;

    /* after tune */
    self->connected = 1;
    self->channel_max = self->conn->channel_max;
    self->frame_max = self->conn->frame_max;
    self->heartbeat = self->conn->heartbeat;
    self->server_properties = AMQTable_toPyDict(amqp_get_server_properties(self->conn));
    Py_RETURN_NONE;
error:
    PyRabbitMQ_Connection_close(self);
bail:
    PyObjectPool_XDECREF(pyobj_pool);

    return 0;
}


/*
 * Connection._close()
 */
static PyObject*
PyRabbitMQ_Connection_close(PyRabbitMQ_Connection *self)
{
    amqp_rpc_reply_t reply;
    if (self->connected) {
        self->connected = 0;

        Py_BEGIN_ALLOW_THREADS
        reply = amqp_connection_close(self->conn, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(self->conn);
        self->sockfd = 0;
        Py_END_ALLOW_THREADS
    }

    Py_RETURN_NONE;
}


unsigned int
PyRabbitMQ_Connection_create_channel(PyRabbitMQ_Connection *self, unsigned int channel)
{
    amqp_rpc_reply_t reply;

    Py_BEGIN_ALLOW_THREADS;
    amqp_channel_open(self->conn, channel);
    reply = amqp_get_rpc_reply(self->conn);
    Py_END_ALLOW_THREADS;

    return PyRabbitMQ_HandleAMQError(self, 0, reply, "Couldn't create channel");
}


/*
 * Connection._channel_open
 */
static PyObject *
PyRabbitMQ_Connection_channel_open(PyRabbitMQ_Connection *self, PyObject *args)
{
    unsigned int channel;

    if (PyRabbitMQ_Not_Connected(self))
        goto bail;

    if (!PyArg_ParseTuple(args, "I", &channel))
        goto bail;

    if (PyRabbitMQ_Connection_create_channel(self, channel))
        goto bail;;

    Py_RETURN_NONE;
bail:
    return 0;
}


/*
 * Connection._channel_close
 */

unsigned int
PyRabbitMQ_Connection_destroy_channel(PyRabbitMQ_Connection *self,
                                      unsigned int channel)
{
    amqp_rpc_reply_t reply;
    Py_BEGIN_ALLOW_THREADS;
    reply = amqp_channel_close(self->conn, channel, AMQP_REPLY_SUCCESS);
    amqp_maybe_release_buffers_on_channel(self->conn, channel);
    Py_END_ALLOW_THREADS;

    return PyRabbitMQ_HandleAMQError(self, channel, reply, "Couldn't close channel");
}

static PyObject*
PyRabbitMQ_Connection_channel_close(PyRabbitMQ_Connection *self,
                                    PyObject *args)
{
    unsigned int channel = 0;

    if (PyRabbitMQ_Not_Connected(self))
        goto error;

    if (!PyArg_ParseTuple(args, "I", &channel))
        goto error;

    if (PyRabbitMQ_Connection_destroy_channel(self, channel))
        goto error;

    Py_RETURN_NONE;

error:
    return 0;
}

_PYRMQ_INLINE int
PyRabbitMQ_ApplyCallback(PyRabbitMQ_Connection *self,
                         PyObject *consumer_tag,
                         PyObject *channel,
                         PyObject *propdict,
                         PyObject *delivery_info,
                         PyObject *view)
{
    int retval = 0;
    PyObject *channel_callbacks = NULL;
    PyObject *callback_for_tag = NULL;
    PyObject *channels = NULL;
    PyObject *channelobj = NULL;
    PyObject *Message = NULL;
    PyObject *message = NULL;
    PyObject *args = NULL;
    PyObject *callback_result = NULL;

    /* self.callbacks */
    if (!(channel_callbacks = PyDict_GetItem(self->callbacks, channel)))
        return -1;

    /* self.callbacks[consumer_tag] */
    if (!(callback_for_tag = PyDict_GetItem(channel_callbacks, consumer_tag)))
        goto error;

    /* self.channels */
    if (!(channels = PyObject_GetAttrString((PyObject *)self, "channels")))
        goto error;

    /* self.channels[channel] */
    if (!(channelobj = PyDict_GetItem(channels, channel)))
        goto error;

    /* message = self.Message(channel, properties, delivery_info, body) */
    Message = BUILD_METHOD_NAME("Message");
    message = PyObject_CallMethodObjArgs((PyObject *)self, Message,
                channelobj, propdict, delivery_info, view, NULL);
    if (!message)
        goto error;

    /* callback(message) */
    if ((args = PyTuple_New(1)) == NULL) {
        Py_DECREF(message);
        goto finally;
    }
    PyTuple_SET_ITEM(args, 0, message);

    callback_result = PyObject_CallObject(callback_for_tag, args);
    Py_XDECREF(callback_result);

    goto finally;
error:
    retval = -1;
finally:
    Py_XDECREF(args);
    Py_XDECREF(channels);
    Py_XDECREF(Message);
    return retval;
}

void
PyRabbitMQ_SetErr_ReceivedFrame(PyRabbitMQ_Connection *self, amqp_frame_t* frame)
{
    static char errstr[512];

    switch(frame->payload.method.id) {
        case AMQP_CHANNEL_CLOSE_METHOD: {
            amqp_channel_close_t *chanm = (amqp_channel_close_t *)frame->payload.method.decoded;
            snprintf(errstr, sizeof(errstr),
                     "channel error %d, message: %.*s",
                     chanm->reply_code,
                     (int) chanm->reply_text.len,
                     (char *) chanm->reply_text.bytes);
            PyErr_SetString(PyRabbitMQExc_ChannelError, errstr);
            PyRabbitMQ_revive_channel(self, frame->channel);
            break;
        }
        case AMQP_CONNECTION_CLOSE_METHOD: {
            amqp_connection_close_t *connm = (amqp_connection_close_t *)frame->payload.method.decoded;
            snprintf(errstr, sizeof(errstr),
                "server connection error %d message: %.*s",
                connm->reply_code,
                (int) connm->reply_text.len,
                (char *) connm->reply_text.bytes);
            PyErr_SetString(PyRabbitMQExc_ConnectionError, errstr);
            PyRabbitMQ_Connection_close(self);
            break;
        }
        default: {
            PyErr_SetString(PyRabbitMQExc_ConnectionError, "Bad frame read");
            break;
        }
    }
}

int
PyRabbitMQ_recv(PyRabbitMQ_Connection *self, PyObject *p,
                amqp_connection_state_t conn, int piggyback)
{
    amqp_frame_t frame;
    amqp_channel_t cur_channel = 0;
    amqp_basic_deliver_t *deliver;
    amqp_basic_properties_t *props;
    Py_ssize_t body_target;
    Py_ssize_t body_received;
    PyObject *channel = NULL;
    PyObject *consumer_tag = NULL;
    PyObject *delivery_info = NULL;
    PyObject *propdict = PyDict_New();
    PyObject *payload = NULL;
    PyObject *view = NULL;
    char *buf = NULL;
    char *bufp = NULL;
    unsigned int i = 0;
    register unsigned int j = 0;
    int retval = 0;

    memset(&props, 0, sizeof(props));

    while (1) {
        if (!piggyback) {
            Py_BEGIN_ALLOW_THREADS;
            amqp_maybe_release_buffers(conn);
            retval = amqp_simple_wait_frame(conn, &frame);
            Py_END_ALLOW_THREADS;
            if (retval < 0) break;
            if (frame.frame_type != AMQP_FRAME_METHOD) continue;
            if (frame.payload.method.id != AMQP_BASIC_DELIVER_METHOD) goto altframe;

            cur_channel = frame.channel;

            delivery_info = PyDict_New();
            deliver = (amqp_basic_deliver_t *)frame.payload.method.decoded;
            /* need consumer tag for later.
             * add delivery info to the delivery_info dict.  */
            amqp_basic_deliver_to_PyDict(delivery_info,
                                         deliver->delivery_tag,
                                         deliver->exchange,
                                         deliver->routing_key,
                                         deliver->redelivered);
            /* add in the consumer_tag */
            consumer_tag = PySTRING_FROM_AMQBYTES(deliver->consumer_tag);
            PyDict_SetItemString(delivery_info, "consumer_tag", consumer_tag);
            Py_XDECREF(consumer_tag);

            piggyback = 0;
        }

        Py_BEGIN_ALLOW_THREADS;
        retval = cur_channel == 0 ?
            amqp_simple_wait_frame(conn, &frame) :
            amqp_simple_wait_frame_on_channel(conn, cur_channel, &frame);
        Py_END_ALLOW_THREADS;
        if (retval < 0) break;

        if (frame.frame_type == AMQP_FRAME_METHOD) goto altframe;
        if (frame.frame_type != AMQP_FRAME_HEADER) {
            PyRabbitMQ_SetErr_UnexpectedHeader(&frame);
            goto finally;
        }

        /* if piggybacked, 'channel' is still 0 at this point */
        cur_channel = frame.channel;

        /* channel */
        channel = PyInt_FromLong((unsigned long)frame.channel);

        /* properties */
        props = (amqp_basic_properties_t *)frame.payload.properties.decoded;
        basic_properties_to_PyDict(props, propdict);

        /* body */
        body_target = frame.payload.properties.body_size;
        body_received = 0;

        for (i = 0; body_received < body_target; i++) {
            Py_BEGIN_ALLOW_THREADS;
            retval = amqp_simple_wait_frame_on_channel(conn, cur_channel, &frame);
            Py_END_ALLOW_THREADS;
            if (retval < 0) break;

            if (frame.frame_type == AMQP_FRAME_METHOD) goto altframe;
            if (frame.frame_type != AMQP_FRAME_BODY) {
                PyErr_SetString(PyRabbitMQExc_ChannelError,
                    "Expected body, got unexpected frame");
                goto finally;
            }
            bufp           = frame.payload.body_fragment.bytes;
            body_received += frame.payload.body_fragment.len;
            if (!i) {
                if (body_received < body_target) {
                    payload = PyBytes_FromStringAndSize(NULL,
                                       (Py_ssize_t)body_target);
                    if (!payload)
                        goto finally;
                    buf = PyBytes_AsString(payload);
                    if (!buf)
                        goto finally;
                    view = PyMemoryView_FromObject(payload);
                }
                else {
                    if (p) {
                        payload = PySTRING_FROM_AMQBYTES(
                                    frame.payload.body_fragment);
                    } else {
                        view = buffer_toMemoryView(bufp, (Py_ssize_t)frame.payload.body_fragment.len);
                    }
                    break;
                }
            }
            for (j = 0;
                 j < frame.payload.body_fragment.len;
                 *buf++ = *bufp++, j++);
        }
        if (p) {
            if (!payload) {
                /* expected content, got none */
                if (body_target) goto error;

                /* did not expect content, return empty string */
                payload = PyBytes_FromStringAndSize(NULL, 0);
            }

            PyDict_SetItemString(p, "properties", propdict);
            PyDict_SetItemString(p, "body", payload);
            PyDict_SetItemString(p, "channel", channel);
            retval = 0;
        } else {
            if (!view) goto error;
            retval = PyRabbitMQ_ApplyCallback(self,
                        consumer_tag, channel, propdict, delivery_info, view);
        }
        break;
    }
    goto finally;

altframe:
    PyRabbitMQ_SetErr_ReceivedFrame(self, &frame);
error:
    retval = -1;

finally:
    Py_XDECREF(payload);
    Py_XDECREF(channel);
    Py_XDECREF(propdict);
    Py_XDECREF(delivery_info);
    Py_XDECREF(view);
    return retval;
}


/*
 * Connection._queue_bind
 */
static PyObject*
PyRabbitMQ_Connection_queue_bind(PyRabbitMQ_Connection *self,
                                 PyObject *args)
{
    unsigned int channel;
    PyObject *queue = NULL;
    PyObject *exchange = NULL;
    PyObject *routing_key = NULL;
    PyObject *arguments = NULL;

    amqp_table_t bargs = amqp_empty_table;
    amqp_pool_t *channel_pool = NULL;
    amqp_rpc_reply_t reply;

    pyobject_array_t pyobj_array = {0};
    pyobject_pool_t *pyobj_pool = NULL;

    if (PyRabbitMQ_Not_Connected(self))
        goto bail;

    if (!PyArg_ParseTuple(args, "IOOOO",
            &channel, &queue, &exchange, &routing_key, &arguments))
        goto bail;
    if ((queue = PyObjectArray_Maybe_Unicode(queue, &pyobj_array)) == NULL) goto bail;
    if ((exchange = PyObjectArray_Maybe_Unicode(exchange, &pyobj_array)) == NULL) goto bail;
    if ((routing_key = PyObjectArray_Maybe_Unicode(routing_key, &pyobj_array)) == NULL) goto bail;

    channel_pool = amqp_get_or_create_channel_pool(self->conn, (amqp_channel_t)channel);
    if (channel_pool == NULL) {
        PyErr_NoMemory();
        goto bail;
    }
    pyobj_pool = PyObjectPool_New(channel_pool);
    bargs = PyDict_ToAMQTable(self->conn, arguments, channel_pool, pyobj_pool);
    if (PyErr_Occurred())
        goto bail;

    Py_BEGIN_ALLOW_THREADS;
    amqp_queue_bind(self->conn, channel,
                        PyString_AS_AMQBYTES(queue),
                        PyString_AS_AMQBYTES(exchange),
                        PyString_AS_AMQBYTES(routing_key),
                        bargs);
    reply = amqp_get_rpc_reply(self->conn);
    PyObjectPool_XDECREF(pyobj_pool);
    amqp_maybe_release_buffers_on_channel(self->conn, channel);
    PyObjectArray_XDECREF(&pyobj_array);
    Py_END_ALLOW_THREADS;

    if (PyRabbitMQ_HandleAMQError(self, channel, reply, "queue.bind"))
        goto bail;

    Py_RETURN_NONE;
bail:
    PyObjectPool_XDECREF(pyobj_pool);
    PyObjectArray_XDECREF(&pyobj_array);

    return 0;
}


/*
 * Connection._queue_unbind
 */
static PyObject*
PyRabbitMQ_Connection_queue_unbind(PyRabbitMQ_Connection *self,
                                   PyObject *args)
{
    unsigned int channel;
    PyObject *queue = NULL;
    PyObject *exchange = NULL;
    PyObject *routing_key = NULL;
    PyObject *arguments = NULL;

    amqp_table_t uargs = amqp_empty_table;
    amqp_pool_t *channel_pool = NULL;
    amqp_rpc_reply_t reply;

    pyobject_array_t pyobj_array = {0};
    pyobject_pool_t *pyobj_pool = NULL;

    if (PyRabbitMQ_Not_Connected(self))
        goto bail;

    if (!PyArg_ParseTuple(args, "IOOOO",
            &channel, &queue, &exchange, &routing_key, &arguments))
        goto bail;
    if ((queue = PyObjectArray_Maybe_Unicode(queue, &pyobj_array)) == NULL) goto bail;
    if ((exchange = PyObjectArray_Maybe_Unicode(exchange, &pyobj_array)) == NULL) goto bail;
    if ((routing_key = PyObjectArray_Maybe_Unicode(routing_key, &pyobj_array)) == NULL) goto bail;

    channel_pool = amqp_get_or_create_channel_pool(self->conn, (amqp_channel_t)channel);
    if (channel_pool == NULL) {
        PyErr_NoMemory();
        goto bail;
    }
    pyobj_pool = PyObjectPool_New(channel_pool);
    uargs = PyDict_ToAMQTable(self->conn, arguments, channel_pool, pyobj_pool);
    if (PyErr_Occurred())
        goto bail;

    Py_BEGIN_ALLOW_THREADS;
    amqp_queue_unbind(self->conn, channel,
                      PyString_AS_AMQBYTES(queue),
                      PyString_AS_AMQBYTES(exchange),
                      PyString_AS_AMQBYTES(routing_key),
                      uargs);
    reply = amqp_get_rpc_reply(self->conn);
    PyObjectPool_XDECREF(pyobj_pool);
    amqp_maybe_release_buffers_on_channel(self->conn, channel);
    PyObjectArray_XDECREF(&pyobj_array);
    Py_END_ALLOW_THREADS;

    if (PyRabbitMQ_HandleAMQError(self, channel, reply, "queue.unbind"))
        goto bail;

    Py_RETURN_NONE;
bail:
    PyObjectPool_XDECREF(pyobj_pool);
    PyObjectArray_XDECREF(&pyobj_array);

    return 0;
}

/*
 * Connection._queue_delete
 */
static PyObject*
PyRabbitMQ_Connection_queue_delete(PyRabbitMQ_Connection *self,
                                   PyObject *args)
{
    PyObject *queue = NULL;
    unsigned int channel = 0;
    unsigned int if_unused = 0;
    unsigned int if_empty = 0;

    amqp_queue_delete_ok_t *ok;
    amqp_rpc_reply_t reply;

    pyobject_array_t pyobj_array = {0};

    if (PyRabbitMQ_Not_Connected(self))
        goto bail;

    if (!PyArg_ParseTuple(args, "IOII",
            &channel, &queue, &if_unused, &if_empty))
        goto bail;
    if ((queue = PyObjectArray_Maybe_Unicode(queue, &pyobj_array)) == NULL) goto bail;

    Py_BEGIN_ALLOW_THREADS;
    ok = amqp_queue_delete(self->conn, channel,
            PyString_AS_AMQBYTES(queue),
            (amqp_boolean_t)if_unused,
            (amqp_boolean_t)if_empty);
    if (ok == NULL)
        reply = amqp_get_rpc_reply(self->conn);
    amqp_maybe_release_buffers_on_channel(self->conn, channel);
    PyObjectArray_XDECREF(&pyobj_array);
    Py_END_ALLOW_THREADS;

    if (ok == NULL && PyRabbitMQ_HandleAMQError(self, channel,
            reply, "queue.delete"))
        goto bail;

    return PyInt_FromLong((long)ok->message_count);
bail:
    PyObjectArray_XDECREF(&pyobj_array);
    return 0;
}


/*
 * Connection._queue_declare
 */
static PyObject*
PyRabbitMQ_Connection_queue_declare(PyRabbitMQ_Connection *self,
                                    PyObject *args)
{
    PyObject *queue = NULL;
    PyObject *arguments = NULL;
    unsigned int channel = 0;
    unsigned int passive = 0;
    unsigned int durable = 0;
    unsigned int exclusive = 0;
    unsigned int auto_delete = 0;

    amqp_queue_declare_ok_t *ok;
    amqp_rpc_reply_t reply;
    amqp_pool_t *channel_pool = NULL;
    amqp_table_t qargs = amqp_empty_table;
    PyObject *ret = NULL;

    pyobject_array_t pyobj_array = {0};
    pyobject_pool_t *pyobj_pool = NULL;

    if (PyRabbitMQ_Not_Connected(self))
        goto bail;

    if (!PyArg_ParseTuple(args, "IOIIIIO",
            &channel, &queue, &passive, &durable,
            &exclusive, &auto_delete, &arguments))
        goto bail;
    if ((queue = PyObjectArray_Maybe_Unicode(queue, &pyobj_array)) == NULL) goto bail;
    channel_pool = amqp_get_or_create_channel_pool(self->conn, (amqp_channel_t)channel);
    if (channel_pool == NULL) {
        PyErr_NoMemory();
        goto bail;
    }
    pyobj_pool = PyObjectPool_New(channel_pool);
    qargs = PyDict_ToAMQTable(self->conn, arguments, channel_pool, pyobj_pool);
    if (PyErr_Occurred())
        goto bail;

    Py_BEGIN_ALLOW_THREADS;
    ok = amqp_queue_declare(self->conn, channel,
                            PyString_AS_AMQBYTES(queue),
                            (amqp_boolean_t)passive,
                            (amqp_boolean_t)durable,
                            (amqp_boolean_t)exclusive,
                            (amqp_boolean_t)auto_delete,
                            qargs
    );
    reply = amqp_get_rpc_reply(self->conn);
    PyObjectPool_XDECREF(pyobj_pool);
    PyObjectArray_XDECREF(&pyobj_array);
    Py_END_ALLOW_THREADS;

    if (PyRabbitMQ_HandleAMQError(self, channel, reply, "queue.declare"))
        goto bail;

    if ((ret = PyTuple_New(3)) == NULL) goto bail;
    PyTuple_SET_ITEM(ret, 0, PySTRING_FROM_AMQBYTES(ok->queue));
    PyTuple_SET_ITEM(ret, 1, PyInt_FromLong((long)ok->message_count));
    PyTuple_SET_ITEM(ret, 2, PyInt_FromLong((long)ok->consumer_count));
    return ret;
bail:
    PyObjectPool_XDECREF(pyobj_pool);
    PyObjectArray_XDECREF(&pyobj_array);

    return 0;
}


/*
 * Connection._queue_purge
 */
static PyObject*
PyRabbitMQ_Connection_queue_purge(PyRabbitMQ_Connection *self,
                                  PyObject *args)
{
    PyObject *queue = NULL;
    unsigned int channel = 0;
    unsigned int no_wait = 0;

    amqp_queue_purge_ok_t *ok;
    amqp_rpc_reply_t reply;

    pyobject_array_t pyobj_array = {0};

    if (PyRabbitMQ_Not_Connected(self))
        goto bail;

    if (!PyArg_ParseTuple(args, "IOI", &channel, &queue, &no_wait))
        goto bail;
    if ((queue = PyObjectArray_Maybe_Unicode(queue, &pyobj_array)) == NULL) goto bail;

    Py_BEGIN_ALLOW_THREADS;
    ok = amqp_queue_purge(self->conn, channel,
                          PyString_AS_AMQBYTES(queue));
    reply = amqp_get_rpc_reply(self->conn);
    amqp_maybe_release_buffers_on_channel(self->conn, channel);
    PyObjectArray_XDECREF(&pyobj_array);
    Py_END_ALLOW_THREADS;

    if (PyRabbitMQ_HandleAMQError(self, channel, reply, "queue.purge"))
        goto bail;

    return PyInt_FromLong((long)ok->message_count);
bail:
    PyObjectArray_XDECREF(&pyobj_array);
    return 0;
}


/*
 * Connection._exchange_declare
 */
static PyObject*
PyRabbitMQ_Connection_exchange_declare(PyRabbitMQ_Connection *self,
                                       PyObject *args)
{
    unsigned int channel = 0;
    PyObject *exchange = NULL;
    PyObject *type = NULL;
    PyObject *arguments = 0;
    unsigned int passive = 0;
    unsigned int durable = 0;
    /* auto_delete argument is ignored,
     * as it has been decided that it's not that useful after all. */
    unsigned int auto_delete = 0;

    amqp_table_t eargs = amqp_empty_table;
    amqp_pool_t *channel_pool = NULL;
    amqp_rpc_reply_t reply;

    pyobject_array_t pyobj_array = {0};
    pyobject_pool_t *pyobj_pool = NULL;

    if (PyRabbitMQ_Not_Connected(self))
        goto bail;

    if (!PyArg_ParseTuple(args, "IOOIIIO",
            &channel, &exchange, &type, &passive,
            &durable, &auto_delete, &arguments))
        goto bail;
    if ((exchange = PyObjectArray_Maybe_Unicode(exchange, &pyobj_array)) == NULL) goto bail;
    if ((type = PyObjectArray_Maybe_Unicode(type, &pyobj_array)) == NULL) goto bail;

    channel_pool = amqp_get_or_create_channel_pool(self->conn, (amqp_channel_t)channel);
    if (channel_pool == NULL) {
        PyErr_NoMemory();
        goto bail;
    }

    pyobj_pool = PyObjectPool_New(channel_pool);
    eargs = PyDict_ToAMQTable(self->conn, arguments, channel_pool, pyobj_pool);
    if (PyErr_Occurred())
        goto bail;

    Py_BEGIN_ALLOW_THREADS;
    amqp_exchange_declare(self->conn, channel,
                          PyString_AS_AMQBYTES(exchange),
                          PyString_AS_AMQBYTES(type),
                          (amqp_boolean_t)passive,
                          (amqp_boolean_t)durable,
                          0, 0, eargs
    );
    reply = amqp_get_rpc_reply(self->conn);
    PyObjectPool_XDECREF(pyobj_pool);
    PyObjectArray_XDECREF(&pyobj_array);
    Py_END_ALLOW_THREADS;

    if (PyRabbitMQ_HandleAMQError(self, channel, reply, "exchange.declare"))
        goto bail;
    Py_RETURN_NONE;
bail:
    PyObjectPool_XDECREF(pyobj_pool);
    PyObjectArray_XDECREF(&pyobj_array);

    return 0;
}

/*
 * Connection._exchange_delete
 */
static PyObject*
PyRabbitMQ_Connection_exchange_delete(PyRabbitMQ_Connection *self,
                                      PyObject *args)
{
    PyObject *exchange = NULL;
    unsigned int channel = 0;
    unsigned int if_unused = 0;

    amqp_rpc_reply_t reply;

    pyobject_array_t pyobj_array = {0};

    if (PyRabbitMQ_Not_Connected(self))
        goto bail;

    if (!PyArg_ParseTuple(args, "IOI", &channel, &exchange, &if_unused))
        goto bail;
    if ((exchange = PyObjectArray_Maybe_Unicode(exchange, &pyobj_array)) == NULL) goto bail;

    Py_BEGIN_ALLOW_THREADS;
    amqp_exchange_delete(self->conn, channel,
                         PyString_AS_AMQBYTES(exchange),
                         (amqp_boolean_t)if_unused);
    reply = amqp_get_rpc_reply(self->conn);
    amqp_maybe_release_buffers_on_channel(self->conn, channel);
    PyObjectArray_XDECREF(&pyobj_array);
    Py_END_ALLOW_THREADS;

    if (PyRabbitMQ_HandleAMQError(self, channel, reply, "exchange.delete"))
        goto bail;

    Py_RETURN_NONE;
bail:
    PyObjectArray_XDECREF(&pyobj_array);

    return 0;
}

/*
 * Connection._basic_publish
 */
static PyObject*
PyRabbitMQ_Connection_basic_publish(PyRabbitMQ_Connection *self,
                                    PyObject *args)
{
    PyObject *exchange = NULL;
    PyObject *routing_key = NULL;
    PyObject *propdict;
    unsigned int channel = 0;
    unsigned int mandatory = 0;
    unsigned int immediate = 0;

    char *body_buf = NULL;
    Py_ssize_t body_size = 0;

    int ret = 0;
    amqp_basic_properties_t props;
    amqp_bytes_t bytes;
    amqp_pool_t *channel_pool = NULL;
    memset(&props, 0, sizeof(props));

    pyobject_array_t pyobj_array = {0};
    pyobject_pool_t *pyobj_pool = NULL;

    if (PyRabbitMQ_Not_Connected(self))
        goto bail;

    if (!PyArg_ParseTuple(args, "Is#OOO|II",
            &channel, &body_buf, &body_size, &exchange, &routing_key,
            &propdict, &mandatory, &immediate))
        goto bail;

    if ((exchange = PyObjectArray_Maybe_Unicode(exchange, &pyobj_array)) == NULL) goto bail;
    if ((routing_key = PyObjectArray_Maybe_Unicode(routing_key, &pyobj_array)) == NULL) goto bail;

    Py_INCREF(propdict);
    channel_pool = amqp_get_or_create_channel_pool(self->conn, (amqp_channel_t)channel);

    pyobj_pool = PyObjectPool_New(channel_pool);
    if (PyDict_to_basic_properties(propdict, &props, self->conn, channel_pool, pyobj_pool) < 1) {
        goto bail;
    }
    Py_DECREF(propdict);

    bytes.len = (size_t)body_size;
    bytes.bytes = (void *)body_buf;

    Py_BEGIN_ALLOW_THREADS;
    ret = amqp_basic_publish(self->conn, channel,
                             PyString_AS_AMQBYTES(exchange),
                             PyString_AS_AMQBYTES(routing_key),
                             (amqp_boolean_t)mandatory,
                             (amqp_boolean_t)immediate,
                             &props,
                             bytes);
    PyObjectPool_XDECREF(pyobj_pool);
    amqp_maybe_release_buffers_on_channel(self->conn, channel);
    PyObjectArray_XDECREF(&pyobj_array);
    Py_END_ALLOW_THREADS;

    if (!PyRabbitMQ_HandleError(ret, "basic.publish")) {
        goto error;
    }
    Py_RETURN_NONE;

error:
    PyRabbitMQ_revive_channel(self, channel);
bail:
    PyObjectPool_XDECREF(pyobj_pool);
    PyObjectArray_XDECREF(&pyobj_array);

    return 0;
}


/*
 * Connection._basic_ack
 */
static PyObject*
PyRabbitMQ_Connection_basic_ack(PyRabbitMQ_Connection *self,
                                PyObject *args)
{
    Py_ssize_t delivery_tag = 0;
    unsigned int channel = 0;
    unsigned int multiple = 0;
    int ret = 0;

    if (PyRabbitMQ_Not_Connected(self))
        goto bail;

    if (!PyArg_ParseTuple(args, "InI", &channel, &delivery_tag, &multiple))
        goto bail;

    Py_BEGIN_ALLOW_THREADS;
    ret = amqp_basic_ack(self->conn, channel,
                        (uint64_t)delivery_tag,
                        (amqp_boolean_t)multiple);
    Py_END_ALLOW_THREADS;

    if (!PyRabbitMQ_HandleError(ret, "basic.ack"))
        goto error;

    Py_RETURN_NONE;
error:
    PyRabbitMQ_revive_channel(self, channel);
bail:
    return 0;
}

/*
 * Connection._basic_reject
 */
static PyObject *PyRabbitMQ_Connection_basic_reject(PyRabbitMQ_Connection *self,
                                                    PyObject *args)
{
    Py_ssize_t delivery_tag = 0;
    unsigned int channel = 0;
    unsigned int multiple = 0;

    int ret = 0;

    if (PyRabbitMQ_Not_Connected(self))
        goto bail;

    if (!PyArg_ParseTuple(args, "InI", &channel, &delivery_tag, &multiple))
        goto bail;

    Py_BEGIN_ALLOW_THREADS;
    ret = amqp_basic_reject(self->conn, channel,
                            (uint64_t)delivery_tag,
                            (amqp_boolean_t)multiple);
    Py_END_ALLOW_THREADS;

    if (!PyRabbitMQ_HandleError(ret, "basic.reject"))
        goto error;

    Py_RETURN_NONE;
error:
    PyRabbitMQ_revive_channel(self, channel);
bail:
    return 0;
}


/*
 * Connection._basic_cancel
 */
static PyObject*
PyRabbitMQ_Connection_basic_cancel(PyRabbitMQ_Connection *self,
                                   PyObject *args)
{
    PyObject *consumer_tag = NULL;
    unsigned int channel = 0;

    amqp_basic_cancel_ok_t *ok;
    amqp_rpc_reply_t reply;

    pyobject_array_t pyobj_array = {0};

    if (PyRabbitMQ_Not_Connected(self))
        goto ok;

    if (!PyArg_ParseTuple(args, "IO", &channel, &consumer_tag))
        goto bail;
    if ((consumer_tag = PyObjectArray_Maybe_Unicode(consumer_tag, &pyobj_array)) == NULL) goto bail;

    Py_BEGIN_ALLOW_THREADS;
    ok = amqp_basic_cancel(self->conn, channel,
                           PyString_AS_AMQBYTES(consumer_tag));
    reply = amqp_get_rpc_reply(self->conn);
    amqp_maybe_release_buffers_on_channel(self->conn, channel);
    PyObjectArray_XDECREF(&pyobj_array);
    Py_END_ALLOW_THREADS;

    if (PyRabbitMQ_HandleAMQError(self, channel, reply, "basic.cancel"))
        goto bail;

ok:
    Py_RETURN_NONE;
bail:
    PyObjectArray_XDECREF(&pyobj_array);

    return 0;
}


/*
 * Connection._basic_consume
 */
static PyObject*
PyRabbitMQ_Connection_basic_consume(PyRabbitMQ_Connection *self,
                                    PyObject *args)
{
    PyObject *queue = NULL;
    PyObject *consumer_tag = NULL;
    PyObject *arguments = NULL;
    unsigned int channel = 0;
    unsigned int no_local = 0;
    unsigned int no_ack = 0;
    unsigned int exclusive = 0;

    amqp_basic_consume_ok_t *ok;
    amqp_rpc_reply_t reply;
    amqp_pool_t *channel_pool = NULL;
    amqp_table_t cargs = amqp_empty_table;

    pyobject_array_t pyobj_array = {0};
    pyobject_pool_t *pyobj_pool = NULL;

    if (PyRabbitMQ_Not_Connected(self))
        goto bail;

    if (!PyArg_ParseTuple(args, "IOOIIIO",
            &channel, &queue, &consumer_tag, &no_local,
            &no_ack, &exclusive, &arguments))
        goto bail;
    if ((queue = PyObjectArray_Maybe_Unicode(queue, &pyobj_array)) == NULL) goto bail;
    if ((consumer_tag = PyObjectArray_Maybe_Unicode(consumer_tag, &pyobj_array)) == NULL) goto bail;

    channel_pool = amqp_get_or_create_channel_pool(self->conn, (amqp_channel_t)channel);
    if (channel_pool == NULL) {
        PyErr_NoMemory();
        goto bail;
    }

    pyobj_pool = PyObjectPool_New(channel_pool);
    cargs = PyDict_ToAMQTable(self->conn, arguments, channel_pool, pyobj_pool);
    if (PyErr_Occurred())
        goto bail;

    Py_BEGIN_ALLOW_THREADS;
    ok = amqp_basic_consume(self->conn, channel,
                            PyString_AS_AMQBYTES(queue),
                            PyString_AS_AMQBYTES(consumer_tag),
                            no_local,
                            no_ack,
                            exclusive,
                            cargs);
    reply = amqp_get_rpc_reply(self->conn);
    PyObjectPool_XDECREF(pyobj_pool);
    PyObjectArray_XDECREF(&pyobj_array);
    Py_END_ALLOW_THREADS;

    if (PyRabbitMQ_HandleAMQError(self, channel, reply, "basic.consume"))
        goto bail;

    return PySTRING_FROM_AMQBYTES(ok->consumer_tag);
bail:
    PyObjectPool_XDECREF(pyobj_pool);
    PyObjectArray_XDECREF(&pyobj_array);

    return 0;
}

/*
 * Connection._basic_qos
 */
static PyObject*
PyRabbitMQ_Connection_basic_qos(PyRabbitMQ_Connection *self,
                                PyObject *args)
{
    unsigned int channel = 0;
    Py_ssize_t prefetch_size = 0;
    unsigned int prefetch_count = 0;
    unsigned int _global = 0;

    if (PyRabbitMQ_Not_Connected(self))
        goto error;

    if (!PyArg_ParseTuple(args, "InII",
            &channel, &prefetch_size, &prefetch_count, &_global))
        goto error;

    Py_BEGIN_ALLOW_THREADS;
    amqp_basic_qos(self->conn, channel,
                   (uint32_t)prefetch_size,
                   (uint16_t)prefetch_count,
                   (amqp_boolean_t)_global);
    Py_END_ALLOW_THREADS;

    Py_RETURN_NONE;
error:
    return 0;
}

/*
 * Connection._flow
 */
static PyObject*
PyRabbitMQ_Connection_flow(PyRabbitMQ_Connection *self,
                           PyObject *args)
{
    unsigned int channel = 0;
    unsigned int active  = 1;

    amqp_channel_flow_ok_t *ok;
    amqp_rpc_reply_t reply;

    if (PyRabbitMQ_Not_Connected(self))
        goto bail;

    if (!PyArg_ParseTuple(args, "II", &channel, &active))
        goto bail;

    Py_BEGIN_ALLOW_THREADS;
    ok = amqp_channel_flow(self->conn, channel, (amqp_boolean_t)active);
    reply = amqp_get_rpc_reply(self->conn);
    amqp_maybe_release_buffers_on_channel(self->conn, channel);
    Py_END_ALLOW_THREADS;

    if (PyRabbitMQ_HandleAMQError(self, channel, reply, "channel.flow"))
        goto bail;

    Py_RETURN_NONE;
bail:
    return 0;
}

/*
 * Connection._basic_recover
 */
static PyObject*
PyRabbitMQ_Connection_basic_recover(PyRabbitMQ_Connection *self,
                                    PyObject *args)
{
    unsigned int channel = 0;
    unsigned int requeue = 0;

    amqp_basic_recover_ok_t *ok;
    amqp_rpc_reply_t reply;

    if (PyRabbitMQ_Not_Connected(self))
        goto bail;

    if (!PyArg_ParseTuple(args, "II", &channel, &requeue))
        goto bail;

    Py_BEGIN_ALLOW_THREADS;
    ok = amqp_basic_recover(self->conn, channel, (amqp_boolean_t)requeue);
    reply = amqp_get_rpc_reply(self->conn);
    amqp_maybe_release_buffers_on_channel(self->conn, channel);
    Py_END_ALLOW_THREADS;

    if (PyRabbitMQ_HandleAMQError(self, channel, reply, "basic.recover"))
        goto bail;

    Py_RETURN_NONE;
bail:
    return 0;
}


/*
 * Connection._basic_recv
 */
static PyObject*
PyRabbitMQ_Connection_basic_recv(PyRabbitMQ_Connection *self,
                                 PyObject *args)
{
    int ready = 0;
    double timeout;

    if (PyRabbitMQ_Not_Connected(self))
        goto bail;

    if (!PyArg_ParseTuple(args, "d", &timeout))
        goto bail;

    if (PYRMQ_SHOULD_POLL(timeout) && !AMQP_ACTIVE_BUFFERS(self->conn)) {
        Py_BEGIN_ALLOW_THREADS;
        ready = RabbitMQ_WAIT(self->sockfd, timeout);
        Py_END_ALLOW_THREADS;
        if (PyRabbitMQ_HandlePollError(ready) <= 0)
            goto bail;
    }

    if (PyRabbitMQ_recv(self, NULL, self->conn, 0) < 0) {
        if (!PyErr_Occurred())
            PyErr_SetString(PyRabbitMQExc_ConnectionError, "Bad frame read");
        goto error;
    }

    Py_RETURN_NONE;
error:
    PyRabbitMQ_Connection_close(self);
bail:
    return 0;
}

/*
 * Connection.__repr__
 */
static PyObject *
PyRabbitMQ_Connection_repr(PyRabbitMQ_Connection *self)
{
    return FROM_FORMAT("<Connection: %s:%s@%s:%d/%s>",
                       self->userid, self->password, self->hostname,
                       self->port, self->virtual_host);
}


/* Connection._basic_get */
static PyObject*
PyRabbitMQ_Connection_basic_get(PyRabbitMQ_Connection *self,
                                PyObject *args)
{
    PyObject *queue = NULL;
    unsigned int channel = 0;
    unsigned int no_ack = 0;

    amqp_rpc_reply_t reply;
    amqp_basic_get_ok_t *ok = NULL;
    PyObject *p = NULL;
    PyObject *delivery_info = NULL;
    PyObject *message_count = NULL;

    pyobject_array_t pyobj_array = {0};

    if (PyRabbitMQ_Not_Connected(self))
        goto bail;

    if (!PyArg_ParseTuple(args, "IOI", &channel, &queue, &no_ack))
        goto bail;
    if ((queue = PyObjectArray_Maybe_Unicode(queue, &pyobj_array)) == NULL) goto bail;

    Py_BEGIN_ALLOW_THREADS;
    reply = amqp_basic_get(self->conn, channel,
                           PyString_AS_AMQBYTES(queue),
                           (amqp_boolean_t)no_ack);
    PyObjectArray_XDECREF(&pyobj_array);
    Py_END_ALLOW_THREADS;

    if (PyRabbitMQ_HandleAMQError(self, channel, reply, "basic.get"))
        goto bail;
    if (reply.reply.id != AMQP_BASIC_GET_OK_METHOD)
        goto empty;

    ok = (amqp_basic_get_ok_t *)reply.reply.decoded;
    p = PyDict_New();

    /* p["delivery_info"] = {} */
    delivery_info = PyDict_New();
    PyDICT_SETSTR_DECREF(p, "delivery_info", delivery_info);
    amqp_basic_deliver_to_PyDict(delivery_info,
                                 ok->delivery_tag,
                                 ok->exchange,
                                 ok->routing_key,
                                 ok->redelivered);
    /* add in the message_count */
    message_count = PyLong_FromLong(ok->message_count);
    PyDict_SetItemString(delivery_info, "message_count", message_count);
    Py_XDECREF(message_count);

    if (amqp_data_in_buffer(self->conn)) {
        if (PyRabbitMQ_recv(self, p, self->conn, 1) < 0) {
            if (!PyErr_Occurred())
                PyErr_SetString(PyRabbitMQExc_ConnectionError,
                        "Bad frame read");
            Py_XDECREF(p);
            Py_XDECREF(delivery_info);
            goto error;
        }
    }
    return p;
error:
    PyRabbitMQ_Connection_close(self);
bail:
    PyObjectArray_XDECREF(&pyobj_array);

    return 0;
empty:
    Py_RETURN_NONE;
}


/* Module: _librabbitmq */

static PyMethodDef PyRabbitMQ_functions[] = {
    {NULL, NULL, 0, NULL}
};

#if PY_MAJOR_VERSION >= 3
    static struct PyModuleDef PyRabbitMQ_moduledef = {
        PyModuleDef_HEAD_INIT,
        PYRABBITMQ_MODULE_NAME,                /* m_name */
        PYRABBITMQ_MODULE_DESC,                /* m_doc */
        -1,                                    /* m_size */
        PyRabbitMQ_functions,                  /* m_methods */
        NULL,                                  /* m_reload */
        NULL,                                  /* m_traverse */
        NULL,                                  /* m_clear */
        NULL,                                  /* m_free */
    };
#endif

PYRABBITMQ_MOD_INIT(_librabbitmq)
{
    PyObject *module, *socket_module;

    if (PyType_Ready(&PyRabbitMQ_ConnectionType) < 0) {
    #if PY_MAJOR_VERSION >= 3
        return NULL;
    #else
        return;
    #endif
    }

#if PY_MAJOR_VERSION >= 3
    module = PyModule_Create(&PyRabbitMQ_moduledef);
#else
    module = Py_InitModule3(PYRABBITMQ_MODULE_NAME, PyRabbitMQ_functions,
            PYRABBITMQ_MODULE_DESC);
#endif

    if (module == NULL) {
    #if PY_MAJOR_VERSION >= 3
        return NULL;
    #else
        return;
    #endif
    }

    /* Get socket.error */
    socket_module = PyImport_ImportModule("socket");
    if (!socket_module) {
    #if PY_MAJOR_VERSION >= 3
        return NULL;
    #else
        return;
    #endif
    }
    PyRabbitMQ_socket_timeout = PyObject_GetAttrString(socket_module, "timeout");
    Py_XDECREF(socket_module);

    PyModule_AddStringConstant(module, "__version__", PYRABBITMQ_VERSION);
    PyModule_AddStringConstant(module, "__author__", PYRABBITMQ_AUTHOR);
    PyModule_AddStringConstant(module, "__contact__", PYRABBITMQ_CONTACT);
    PyModule_AddStringConstant(module, "__homepage__", PYRABBITMQ_HOMEPAGE);

    Py_INCREF(&PyRabbitMQ_ConnectionType);
    PyModule_AddObject(module, "Connection", (PyObject *)&PyRabbitMQ_ConnectionType);

    PyModule_AddIntConstant(module, "AMQP_SASL_METHOD_PLAIN", AMQP_SASL_METHOD_PLAIN);

    PyRabbitMQExc_ConnectionError = PyErr_NewException(
            "_librabbitmq.ConnectionError", NULL, NULL);
    PyModule_AddObject(module, "ConnectionError",
                       (PyObject *)PyRabbitMQExc_ConnectionError);
    PyRabbitMQExc_ChannelError = PyErr_NewException(
            "_librabbitmq.ChannelError", NULL, NULL);
    PyModule_AddObject(module, "ChannelError",
                       (PyObject *)PyRabbitMQExc_ChannelError);
#if PY_MAJOR_VERSION >= 3
    return module;
#else
    return;
#endif
}
