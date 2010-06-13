#ifndef __PYLIBRABBIT_H__
#define __PYLIBRABBIT_H__

#ifdef PY25
#  define PY_SSIZE_T_CLEAN
#  define PY_SIZE_TYPE Py_ssize_t
#else
#  define PY_SIZE_TYPE int
#endif

#include <Python.h>

#include <amqp.h>
#include <amqp_framing.h>


typedef struct {
    PyObject_HEAD
    amqp_connection_state_t conn;
    char *hostname;
    char *userid;
    char *password;
    char *vhost;
    int port;

    int sockfd;
} PyRabbitMQ_Connection;

/* utils */
void PyDict_to_basic_properties(PyObject *, amqp_basic_properties_t *);

/* Exceptions */
static PyObject *PyRabbitMQExc_ConnectionError;
static PyObject *PyRabbitMQExc_ChannelError;

/* Connection */
static PyRabbitMQ_Connection* PyRabbitMQ_ConnectionType_new(PyTypeObject *,
       PyObject *, PyObject *);
static void PyRabbitMQ_ConnectionType_dealloc(PyRabbitMQ_Connection *);
static int PyRabbitMQ_Connection_init(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_connect(PyRabbitMQ_Connection *);
static PyObject *PyRabbitMQ_Connection_close(PyRabbitMQ_Connection *);
static PyObject *PyRabbitMQ_Connection_channel_open(PyRabbitMQ_Connection *, PyObject *);
static PyObject *PyRabbitMQ_Connection_channel_close(PyRabbitMQ_Connection *, PyObject *);
static PyObject *PyRabbitMQ_Connection_basic_publish(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);
static PyObject *PyRabbitMQ_Connection_exchange_declare(PyRabbitMQ_Connection *,
        PyObject *, PyObject *);

static PyMethodDef PyRabbitMQ_ConnectionType_methods[] = {
    {"connect", (PyCFunction)PyRabbitMQ_Connection_connect, METH_NOARGS,
        "Establish connection to the server."},
    {"close", (PyCFunction)PyRabbitMQ_Connection_close, METH_NOARGS,
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
        "Delcare an exchange"},
    {NULL, NULL, 0, NULL}
};

static PyTypeObject PyRabbitMQ_ConnectionType = {
    PyObject_HEAD_INIT(NULL)
    0,
    "connection",
    sizeof(PyRabbitMQ_Connection),
    0,
    (destructor)PyRabbitMQ_ConnectionType_dealloc,

    0,
    0,
    0,
    0,
    0,

    0,
    0,
    0,

    0,
    0,
    0,
    0,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    "rabbitmq connection type",
    0,
    0,
    0,
    0,
    0,
    0,
    PyRabbitMQ_ConnectionType_methods,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    (initproc)PyRabbitMQ_Connection_init,
    0,
    (newfunc)PyRabbitMQ_ConnectionType_new, //PyType_GenericNew,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0
};
#endif /* __PYLIBRABBIT_H__ */
