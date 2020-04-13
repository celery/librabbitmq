#ifndef __PYLIBRABBIT_CONNECTION_H__
#define __PYLIBRABBIT_CONNECTION_H__

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <structmember.h>

#include <amqp.h>
#include <amqp_framing.h>

#if PY_MAJOR_VERSION == 2
# define TP_FLAGS (Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_WEAKREFS)
#else
# define TP_FLAGS (Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE)
#endif

#if PY_MAJOR_VERSION >= 3
    #define PYRABBITMQ_MOD_INIT(name) PyMODINIT_FUNC PyInit_##name(void)
#else
    #define PYRABBITMQ_MOD_INIT(name) PyMODINIT_FUNC init##name(void)
#endif


#if PY_VERSION_HEX >= 0x03000000 /* 3.0 and up */
#  define BUILD_METHOD_NAME PyUnicode_FromString
#  define FROM_FORMAT PyUnicode_FromFormat
#  define PyInt_FromLong PyLong_FromLong
#  define PyInt_AS_LONG PyLong_AsLong
#  define PyInt_Check PyLong_Check
#  define PyInt_FromSsize_t PyLong_FromSsize_t
#  define PyString_INTERN_FROM_STRING PyString_FromString
#else                            /* 2.x */
#  define BUILD_METHOD_NAME PyBytes_FromString
#  define FROM_FORMAT PyString_FromFormat
#  define PyString_INTERN_FROM_STRING PyString_InternFromString
#endif

#if defined __GNUC__ && !defined __GNUC_STDC_INLINE__ && !defined __GNUC_GNU_INLINE__
# define __GNUC_GNU_INLINE__ 1
#endif

#ifndef _PYRMQ_INLINE
# if defined(__llvm__)
/* extern inline breaks on LLVM saying 'symbol not found'
 *  but the inline keyword is only treated as a "mild hint"
 *   to the LLVM optimizer anyway, so we'll just disable it.
 */
#  define _PYRMQ_INLINE
# elif defined(__GNUC__) && !defined(__GNUC_STDC_INLINE__)
#  define _PYRMQ_INLINE extern __inline
# else
#  define _PYRMQ_INLINE extern __inline
# endif
#endif


_PYRMQ_INLINE PyObject*
buffer_toMemoryView(char *buf, Py_ssize_t buf_len) {
        PyObject *view;
#if PY_MAJOR_VERSION == 2
        PyObject *pybuffer;
        pybuffer = PyBuffer_FromMemory(buf, buf_len);
        view = PyMemoryView_FromObject(pybuffer);
        Py_XDECREF(pybuffer);
#else
        view = PyMemoryView_FromMemory(buf, buf_len, PyBUF_READ);
#endif
        return view;
}

#define PyDICT_SETNONE_DECREF(dict, key)                            \
    do {                                                            \
        PyDict_SetItem(dict, key, Py_None);                         \
        Py_XDECREF(key);                                            \
    } while(0)

#define PyDICT_SETSTR_DECREF(dict, key, value)                      \
    do {                                                            \
        PyDict_SetItemString(dict, key, value);                     \
        Py_DECREF(value);                                           \
    } while (0)

#define PyDICT_SETKV_DECREF(dict, key, value)                       \
    do {                                                            \
        PyDict_SetItem(dict, key, value);                           \
        Py_XDECREF(key);                                            \
        Py_XDECREF(value);                                          \
    } while(0)

#if PY_MAJOR_VERSION == 2
#  define PySTRING_FROM_AMQBYTES(member)                                        \
        PyString_FromStringAndSize((member).bytes, (Py_ssize_t)(member).len)
#else
#  define PySTRING_FROM_AMQBYTES(member)                                        \
        PyUnicode_FromStringAndSize((member).bytes, (Py_ssize_t)(member).len)
#endif


#define AMQTable_TO_PYKEY(table, i)                                 \
        PySTRING_FROM_AMQBYTES(table->entries[i].key)

#define PyDICT_SETKEY_AMQTABLE(dict, k, v, table, stmt)             \
        PyDICT_SETSTRKEY_DECREF(dict, k, v,                         \
            PySTRING_FROM_AMQBYTES(table->headers.entries[i].key),  \
            stmt);                                                  \

_PYRMQ_INLINE PyObject* Maybe_Unicode(PyObject *);

#if defined(__C99__) || defined(__GNUC__)
#  define PyString_AS_AMQBYTES(s)                                   \
      (amqp_bytes_t){Py_SIZE(s), (void *)PyBytes_AS_STRING(s)}
#else
_PYRMQ_INLINE amqp_bytes_t PyString_AS_AMQBYTES(PyObject *);
_PYRMQ_INLINE amqp_bytes_t
PyString_AS_AMQBYTES(PyObject *s)
{
    amqp_bytes_t ret;
    ret.len = Py_SIZE(s);
    ret.bytes = (void *)PyBytes_AS_STRING(s);
    /*{Py_SIZE(s), (void *)PyString_AS_STRING(s)};*/
    return ret;
}
#endif

_PYRMQ_INLINE PyObject*
Maybe_Unicode(PyObject *s)
{
    if (PyUnicode_Check(s))
        return PyUnicode_AsASCIIString(s);
    return s;
}

#define PYRMQ_IS_TIMEOUT(t)   (t > 0.0)
#define PYRMQ_IS_NONBLOCK(t)  (t == -1)
#define PYRMQ_SHOULD_POLL(t)  (PYRMQ_IS_TIMEOUT(t) || PYRMQ_IS_NONBLOCK(t))

#define RabbitMQ_WAIT(sockfd, timeout)                              \
    (PYRMQ_IS_TIMEOUT(timeout)                                      \
            ? RabbitMQ_wait_timeout(sockfd, timeout)                \
            : RabbitMQ_wait_nb(sockfd))

#define AMQTable_VAL(table, index, typ)                             \
    table->entries[index].value.value.typ

#define AMQArray_VAL(array, index, typ)                             \
    array->entries[index].value.typ

#define AMQP_ACTIVE_BUFFERS(state)                                  \
    (amqp_data_in_buffer(state) || amqp_frames_enqueued(state))


/* Connection object */
typedef struct {
    PyObject_HEAD
    amqp_connection_state_t conn;
    char *hostname;
    char *userid;
    char *password;
    char *virtual_host;
    int port;
    int frame_max;
    int channel_max;
    int heartbeat;

    int sockfd;
    int connected;

    PyObject *client_properties;
    PyObject *server_properties;
    PyObject *callbacks;    /* {channel_id: {consumer_tag:callback}} */

    PyObject *weakreflist;
} PyRabbitMQ_Connection;

// Keep track of PyObject references with increased reference count.
// Entries are stored in the channel pool.
#define PYOBJECT_POOL_MAX 100
typedef struct pyobject_pool_t_ {
    int num_entries;
    PyObject **entries;
    amqp_pool_t *pool;
    struct pyobject_pool_t_ *next;
} pyobject_pool_t;

int
PyDict_to_basic_properties(PyObject *, amqp_basic_properties_t *,
                           amqp_connection_state_t, amqp_pool_t *,
                           pyobject_pool_t *);

/* Connection method sigs */
static PyRabbitMQ_Connection*
PyRabbitMQ_ConnectionType_new(PyTypeObject *, PyObject *, PyObject *);

static void
PyRabbitMQ_ConnectionType_dealloc(PyRabbitMQ_Connection *);

static int
PyRabbitMQ_ConnectionType_init(PyRabbitMQ_Connection *,
                               PyObject *, PyObject *);

static PyObject*
PyRabbitMQ_Connection_fileno(PyRabbitMQ_Connection *);

static PyObject*
PyRabbitMQ_Connection_connect(PyRabbitMQ_Connection *);

static PyObject*
PyRabbitMQ_Connection_close(PyRabbitMQ_Connection *);

static PyObject*
PyRabbitMQ_Connection_channel_open(PyRabbitMQ_Connection *, PyObject *);

static PyObject*
PyRabbitMQ_Connection_channel_close(PyRabbitMQ_Connection *, PyObject *);

static PyObject*
PyRabbitMQ_Connection_basic_publish(PyRabbitMQ_Connection *, PyObject *);

static PyObject*
PyRabbitMQ_Connection_exchange_declare(PyRabbitMQ_Connection *, PyObject *);

static PyObject*
PyRabbitMQ_Connection_exchange_delete(PyRabbitMQ_Connection *, PyObject *);

static PyObject*
PyRabbitMQ_Connection_queue_delete(PyRabbitMQ_Connection *, PyObject *);

static PyObject*
PyRabbitMQ_Connection_queue_declare(PyRabbitMQ_Connection *, PyObject *);

static PyObject*
PyRabbitMQ_Connection_repr(PyRabbitMQ_Connection *);

static PyObject*
PyRabbitMQ_Connection_queue_bind(PyRabbitMQ_Connection *, PyObject *);

static PyObject*
PyRabbitMQ_Connection_queue_unbind(PyRabbitMQ_Connection *, PyObject *);

static PyObject*
PyRabbitMQ_Connection_basic_get(PyRabbitMQ_Connection *, PyObject *);

static PyObject*
PyRabbitMQ_Connection_queue_purge(PyRabbitMQ_Connection *, PyObject *);

static PyObject*
PyRabbitMQ_Connection_basic_ack(PyRabbitMQ_Connection *, PyObject *);

static PyObject*
PyRabbitMQ_Connection_basic_qos(PyRabbitMQ_Connection *, PyObject *);

static PyObject*
PyRabbitMQ_Connection_basic_reject(PyRabbitMQ_Connection *, PyObject *);

static PyObject*
PyRabbitMQ_Connection_basic_recover(PyRabbitMQ_Connection *, PyObject *);

static PyObject*
PyRabbitMQ_Connection_basic_recv(PyRabbitMQ_Connection *, PyObject *);

static PyObject*
PyRabbitMQ_Connection_basic_consume(PyRabbitMQ_Connection *, PyObject *);

static PyObject*
PyRabbitMQ_Connection_basic_cancel(PyRabbitMQ_Connection *, PyObject *);

static PyObject
*PyRabbitMQ_Connection_flow(PyRabbitMQ_Connection *, PyObject *);


/* Connection attributes */
static PyMemberDef PyRabbitMQ_ConnectionType_members[] = {
    {"hostname", T_STRING,
        offsetof(PyRabbitMQ_Connection, hostname), READONLY, NULL},
    {"userid", T_STRING,
        offsetof(PyRabbitMQ_Connection, userid), READONLY, NULL},
    {"password", T_STRING,
        offsetof(PyRabbitMQ_Connection, password), READONLY, NULL},
    {"virtual_host", T_STRING,
        offsetof(PyRabbitMQ_Connection, virtual_host), READONLY, NULL},
    {"port", T_INT,
        offsetof(PyRabbitMQ_Connection, port), READONLY, NULL},
    {"heartbeat", T_INT,
        offsetof(PyRabbitMQ_Connection, heartbeat), READONLY, NULL},
    {"server_properties", T_OBJECT_EX,
        offsetof(PyRabbitMQ_Connection, server_properties), READONLY, NULL},
    {"connected", T_INT,
        offsetof(PyRabbitMQ_Connection, connected), READONLY, NULL},
    {"channel_max", T_INT,
        offsetof(PyRabbitMQ_Connection, channel_max), READONLY, NULL},
    {"frame_max", T_INT,
        offsetof(PyRabbitMQ_Connection, frame_max), READONLY, NULL},
    {"callbacks", T_OBJECT_EX,
        offsetof(PyRabbitMQ_Connection, callbacks), READONLY, NULL},
    {NULL, 0, 0, 0, NULL}  /* Sentinel */
};

/* Connection methods */
static PyMethodDef PyRabbitMQ_ConnectionType_methods[] = {
    {"fileno", (PyCFunction)PyRabbitMQ_Connection_fileno,
        METH_NOARGS, "File descriptor number."},
    {"connect", (PyCFunction)PyRabbitMQ_Connection_connect,
        METH_NOARGS, "Establish connection to the broker."},
    {"_close", (PyCFunction)PyRabbitMQ_Connection_close,
        METH_NOARGS, "Close connection."},
    {"_channel_open", (PyCFunction)PyRabbitMQ_Connection_channel_open,
        METH_VARARGS, "Create new channel"},
    {"_channel_close", (PyCFunction)PyRabbitMQ_Connection_channel_close,
        METH_VARARGS, "Close channel"},
    {"_basic_publish", (PyCFunction)PyRabbitMQ_Connection_basic_publish,
        METH_VARARGS, "Publish message"},
    {"_exchange_declare", (PyCFunction)PyRabbitMQ_Connection_exchange_declare,
        METH_VARARGS, "Declare an exchange"},
    {"_exchange_delete", (PyCFunction)PyRabbitMQ_Connection_exchange_delete,
        METH_VARARGS, "Delete an exchange"},
    {"_queue_declare", (PyCFunction)PyRabbitMQ_Connection_queue_declare,
        METH_VARARGS, "Declare a queue"},
    {"_queue_bind", (PyCFunction)PyRabbitMQ_Connection_queue_bind,
        METH_VARARGS, "Bind queue"},
    {"_queue_unbind", (PyCFunction)PyRabbitMQ_Connection_queue_unbind,
        METH_VARARGS, "Unbind queue"},
    {"_queue_delete", (PyCFunction)PyRabbitMQ_Connection_queue_delete,
        METH_VARARGS, "Delete queue"},
    {"_basic_get", (PyCFunction)PyRabbitMQ_Connection_basic_get,
        METH_VARARGS, "Try to receive message synchronously."},
    {"_queue_purge", (PyCFunction)PyRabbitMQ_Connection_queue_purge,
        METH_VARARGS, "Purge all messages from queue."},
    {"_basic_ack", (PyCFunction)PyRabbitMQ_Connection_basic_ack,
        METH_VARARGS, "Acknowledge message."},
    {"_basic_reject", (PyCFunction)PyRabbitMQ_Connection_basic_reject,
        METH_VARARGS, "Reject message."},
    {"_basic_qos", (PyCFunction)PyRabbitMQ_Connection_basic_qos,
        METH_VARARGS, "Set Quality of Service settings."},
    {"_flow", (PyCFunction)PyRabbitMQ_Connection_flow,
        METH_VARARGS, "Enable/disable channel flow."},
    {"_basic_recover", (PyCFunction)PyRabbitMQ_Connection_basic_recover,
        METH_VARARGS, "Recover all unacked messages."},
    {"_basic_recv", (PyCFunction)PyRabbitMQ_Connection_basic_recv,
        METH_VARARGS, "Receive events from socket."},
    {"_basic_consume", (PyCFunction)PyRabbitMQ_Connection_basic_consume,
        METH_VARARGS, "Start consuming messages from queue."},
    {"_basic_cancel", (PyCFunction)PyRabbitMQ_Connection_basic_cancel,
        METH_VARARGS, "Cancel consuming from a queue."},
    {NULL, NULL, 0, NULL}
};

/* Connection.__doc__ */
PyDoc_STRVAR(PyRabbitMQ_ConnectionType_doc,
    "AMQP Connection: \n\n"
    "    Connection(hostname='localhost', userid='guest',\n"
    "               password='guest', virtual_host'/',\n"
    "               port=5672, channel_max=0xffff,\n"
    "               frame_max=131072, heartbeat=0).\n\n"
);

static PyTypeObject PyRabbitMQ_ConnectionType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    /* tp_name           */ "_librabbitmq.Connection",
    /* tp_basicsize      */ sizeof(PyRabbitMQ_Connection),
    /* tp_itemsize       */ 0,
    /* tp_dealloc        */ (destructor)PyRabbitMQ_ConnectionType_dealloc,
    /* tp_print          */ 0,
    /* tp_getattr        */ 0,
    /* tp_setattr        */ 0,
    /* tp_compare        */ 0,
    /* tp_repr           */ (reprfunc)PyRabbitMQ_Connection_repr,
    /* tp_as_number      */ 0,
    /* tp_as_sequence    */ 0,
    /* tp_as_mapping     */ 0,
    /* tp_hash           */ 0,
    /* tp_call           */ 0,
    /* tp_str            */ 0,
    /* tp_getattro       */ 0,
    /* tp_setattro       */ 0,
    /* tp_as_buffer      */ 0,
    /* tp_flags          */ TP_FLAGS,
    /* tp_doc            */ PyRabbitMQ_ConnectionType_doc,
    /* tp_traverse       */ 0,
    /* tp_clear          */ 0,
    /* tp_richcompare    */ 0,
    /* tp_weaklistoffset */ offsetof(PyRabbitMQ_Connection, weakreflist),
    /* tp_iter           */ 0,
    /* tp_iternext       */ 0,
    /* tp_methods        */ PyRabbitMQ_ConnectionType_methods,
    /* tp_members        */ PyRabbitMQ_ConnectionType_members,
    /* tp_getset         */ 0,
    /* tp_base           */ 0,
    /* tp_dict           */ 0,
    /* tp_descr_get      */ 0,
    /* tp_descr_set      */ 0,
    /* tp_dictoffset     */ 0,
    /* tp_init           */ (initproc)PyRabbitMQ_ConnectionType_init,
    /* tp_alloc          */ 0,
    /* tp_new            */ (newfunc)PyRabbitMQ_ConnectionType_new,
    /* tp_free           */ 0,
    /* tp_is_gc          */ 0,
    /* tp_bases          */ 0,
    /* tp_mro            */ 0,
    /* tp_cache          */ 0,
    /* tp_subclasses     */ 0,
    /* tp_weaklist       */ 0,
    /* tp_del            */ 0,
    /* tp_version_tag    */ 0x010000000,
};
#endif /* __PYLIBRABBIT_CONNECTION_H__ */
