#include <amqp.h>
#include <Python.h>

#include "distmeta.h"
#include "util.h"

#include "connection.c"

/* Module: _librabbitmq */

//extern PyTypeObject PyRabbitMQ_ConnectionType;

/* Exceptions */
PyObject *PyRabbitMQExc_ConnectionError;
PyObject *PyRabbitMQExc_ChannelError;

PyObject *PyRabbitMQ_socket_timeout;

static PyMethodDef PyRabbitMQ_functions[] = {
    {NULL, NULL, 0, NULL}
};


PyMODINIT_FUNC init_librabbitmq(void)
{
    PyObject *module, *socket_module;

    if (PyType_Ready(&PyRabbitMQ_ConnectionType) < 0) {
        return;
    }

    module = Py_InitModule3("_librabbitmq", PyRabbitMQ_functions,
            "Hand-made wrapper for librabbitmq.");
    if (module == NULL) {
        return;
    }

    /* Get socket.error */
    socket_module = PyImport_ImportModule("socket");
    if (!socket_module)
        return;
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
}
