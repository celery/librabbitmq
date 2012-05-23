#ifndef __PYLIBRABBIT_H__
#define __PYLIBRABBIT_H__

#define PY25
#ifdef PY25
#  define PY_SSIZE_T_CLEAN
#  define PY_SIZE_TYPE Py_ssize_t
#  define PYINT_FROM_SSIZE_T PyInt_FromSsize_t
#  define PYINT_AS_SSIZE_T PyInt_AsSsize_t
#else
#  define PY_SIZE_TYPE long
#  define PYINT_FROM_SSIZE_T PyInt_FromLong
#  define PYINT_AS_SSIZE_T PyInt_AsLong
#endif

#include <Python.h>
#include <structmember.h>

#include <amqp.h>
#include <amqp_framing.h>

#if PY_VERSION_HEX >= 0x03000000
#  define FROM_FORMAT PyUnicode_FromFormat
#  define PyInt_FromLong PyLong_FromLong
#  define PyInt_FromSsize_t PyLong_FromSsize_t
#else
#  define FROM_FORMAT PyString_FromFormat
#endif

/* librabbitmq error codes */
#define ERROR_NO_MEMORY 1
#define ERROR_BAD_AMQP_DATA 2
#define ERROR_UNKNOWN_CLASS 3
#define ERROR_UNKNOWN_METHOD 4
#define ERROR_GETHOSTBYNAME_FAILED 5
#define ERROR_INCOMPATIBLE_AMQP_VERSION 6
#define ERROR_CONNECTION_CLOSED 7
#define ERROR_BAD_AMQP_URL 8
#define ERROR_MAX 8


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

    PyObject *weakreflist;
} PyRabbitMQ_Connection;

/* utils */
void AMQTable_SetStringValue(amqp_connection_state_t,
        amqp_table_t *, amqp_bytes_t, amqp_bytes_t);

void AMQTable_SetIntValue(amqp_connection_state_t,
        amqp_table_t *, amqp_bytes_t, int);

amqp_table_entry_t *AMQTable_AddEntry(amqp_connection_state_t,
        amqp_table_t *, amqp_bytes_t);
int PyDict_to_basic_properties(PyObject *, amqp_basic_properties_t *,
                                amqp_connection_state_t);


/* Exceptions */
static PyObject *PyRabbitMQExc_ConnectionError;
static PyObject *PyRabbitMQExc_ChannelError;

/* Connection */
static PyRabbitMQ_Connection* PyRabbitMQ_ConnectionType_new(PyTypeObject *,
       PyObject *, PyObject *);
static void PyRabbitMQ_ConnectionType_dealloc(PyRabbitMQ_Connection *);
static int PyRabbitMQ_ConnectionType_init(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_fileno(PyRabbitMQ_Connection *);
static PyObject *PyRabbitMQ_Connection_connect(PyRabbitMQ_Connection *);
static PyObject *PyRabbitMQ_Connection_close(PyRabbitMQ_Connection *);
static PyObject *PyRabbitMQ_Connection_channel_open(PyRabbitMQ_Connection *, PyObject *);
static PyObject *PyRabbitMQ_Connection_channel_close(PyRabbitMQ_Connection *, PyObject *);
static PyObject *PyRabbitMQ_Connection_basic_publish(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_exchange_declare(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_exchange_delete(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_queue_delete(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_queue_declare(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_queue_bind(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_repr(PyRabbitMQ_Connection *);
static PyObject *PyRabbitMQ_Connection_queue_unbind(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_basic_get(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_queue_purge(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_basic_ack(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_basic_qos(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_basic_reject(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_basic_recover(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_basic_recv(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_basic_consume(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_basic_cancel(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_flow(PyRabbitMQ_Connection *,
       PyObject *, PyObject *);
static long long PyRabbitMQ_now_usec(void);
static int PyRabbitMQ_wait_timeout(int, double);
static int PyRabbitMQ_wait_nb(int);

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
    {"connected", T_INT,
        offsetof(PyRabbitMQ_Connection, connected), READONLY, NULL},
    {"channel_max", T_INT,
        offsetof(PyRabbitMQ_Connection, channel_max), READONLY, NULL},
    {"frame_max", T_INT,
        offsetof(PyRabbitMQ_Connection, frame_max), READONLY, NULL},
    {NULL}  /* Sentinel */
};


static PyMethodDef PyRabbitMQ_ConnectionType_methods[] = {
    {"fileno", (PyCFunction)PyRabbitMQ_Connection_fileno, METH_NOARGS},
    {"_do_connect", (PyCFunction)PyRabbitMQ_Connection_connect, METH_NOARGS,
        "Establish connection to the server."},
    {"_close", (PyCFunction)PyRabbitMQ_Connection_close, METH_NOARGS,
        "Close connection."},
    {"_channel_open", (PyCFunction)PyRabbitMQ_Connection_channel_open, METH_VARARGS,
        "Create new channel"},
    {"_channel_close", (PyCFunction)PyRabbitMQ_Connection_channel_close, METH_VARARGS,
        "Close channel"},
    {"_basic_publish", (PyCFunction)PyRabbitMQ_Connection_basic_publish,
        METH_VARARGS|METH_KEYWORDS,
        "Publish message"},
    {"_exchange_declare", (PyCFunction)PyRabbitMQ_Connection_exchange_declare,
        METH_VARARGS|METH_KEYWORDS,
        "Declare an exchange"},
    {"_exchange_delete", (PyCFunction)PyRabbitMQ_Connection_exchange_delete,
        METH_VARARGS|METH_KEYWORDS,
        "Delete an exchange"},
    {"_queue_declare", (PyCFunction)PyRabbitMQ_Connection_queue_declare,
        METH_VARARGS|METH_KEYWORDS,
        "Declare a queue"},
    {"_queue_bind", (PyCFunction)PyRabbitMQ_Connection_queue_bind,
        METH_VARARGS|METH_KEYWORDS,
        "Bind queue"},
    {"_queue_unbind", (PyCFunction)PyRabbitMQ_Connection_queue_unbind,
        METH_VARARGS|METH_KEYWORDS,
        "Unbind queue"},
    {"_queue_delete", (PyCFunction)PyRabbitMQ_Connection_queue_delete,
        METH_VARARGS|METH_KEYWORDS,
        "Delete queue"},
    {"_basic_get", (PyCFunction)PyRabbitMQ_Connection_basic_get,
        METH_VARARGS|METH_KEYWORDS,
        "basic.get"},
    {"_queue_purge", (PyCFunction)PyRabbitMQ_Connection_queue_purge,
        METH_VARARGS|METH_KEYWORDS,
        "queue.purge"},
    {"_basic_ack", (PyCFunction)PyRabbitMQ_Connection_basic_ack,
        METH_VARARGS|METH_KEYWORDS,
        "basic.ack"},
    {"_basic_reject", (PyCFunction)PyRabbitMQ_Connection_basic_reject,
        METH_VARARGS|METH_KEYWORDS,
        "basic.reject"},
    {"_basic_qos", (PyCFunction)PyRabbitMQ_Connection_basic_qos,
        METH_VARARGS|METH_KEYWORDS,
        "basic.qos"},
    {"_flow", (PyCFunction)PyRabbitMQ_Connection_flow,
        METH_VARARGS|METH_KEYWORDS,
        "channel.flow"},
    {"_basic_recover", (PyCFunction)PyRabbitMQ_Connection_basic_recover,
        METH_VARARGS|METH_KEYWORDS,
        "basic.recover"},
    {"_basic_recv", (PyCFunction)PyRabbitMQ_Connection_basic_recv,
        METH_VARARGS|METH_KEYWORDS,
        "recv"},
    {"_basic_consume", (PyCFunction)PyRabbitMQ_Connection_basic_consume,
        METH_VARARGS|METH_KEYWORDS,
        "basic.consume"},
    {"_basic_cancel", (PyCFunction)PyRabbitMQ_Connection_basic_cancel,
        METH_VARARGS|METH_KEYWORDS,
        "basic.cancel"},
    {NULL, NULL, 0, NULL}
};


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
    /* tp_flags          */ Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE |
                            Py_TPFLAGS_HAVE_WEAKREFS,
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
};
#endif /* __PYLIBRABBIT_H__ */
