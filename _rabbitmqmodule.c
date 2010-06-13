#include <stdlib.h>
#include <stdio.h>

#include <unistd.h>

#include "_rabbitmqmodule.h"

#define PYRABBITMQ_VERSION "0.0.1"

static PyRabbitMQ_Connection* PyRabbitMQ_ConnectionType_new(PyTypeObject *type,
       PyObject *args, PyObject *kwargs) {
    PyRabbitMQ_Connection *self;

    self = (PyRabbitMQ_Connection *)PyType_GenericNew(type, args, kwargs);

    if (self != NULL) {
        self->conn = NULL;
        self->hostname = NULL;
        self->userid = NULL;
        self->password = NULL;
        self->vhost = NULL;
        self->port = 5672;
        self->sockfd = 0;
    }

    return self;
}

static void PyRabbitMQ_ConnectionType_dealloc(PyRabbitMQ_Connection *self) {
    self->ob_type->tp_free(self);
}

static int PyRabbitMQ_Connection_init(PyRabbitMQ_Connection *self,
        PyObject *args, PyObject *kwargs) {
    char *hostname;
    char *userid;
    char *password;
    char *vhost;
    int port;

    static char *kwlist[] = {"hostname", "userid", "password",
                             "vhost", "port", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ssss|i", kwlist,
                &hostname, &userid, &password, &vhost, &port)) {
        return -1;
    }

    self->hostname = hostname;
    self->userid = userid;
    self->password = password;
    self->vhost = vhost;
    self->port = port;

    printf("hostname: %s userid: %s password: %s, vhost: %s, port: %d\n",
            hostname, userid, password, vhost, port);

    return 0;
}

static PyObject *PyRabbitMQ_Connection_connect(PyRabbitMQ_Connection *self) {
    self->conn = amqp_new_connection();
    self->sockfd = amqp_open_socket(self->hostname, self->port);
    amqp_set_sockfd(self->conn, self->sockfd);
    amqp_login(self->conn, "/", 0, 131072, 0, AMQP_SASL_METHOD_PLAIN,
               "guest", "guest");
    Py_RETURN_NONE;
}

static PyObject *PyRabbitMQ_Connection_close(PyRabbitMQ_Connection *self) {
    amqp_connection_close(self->conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(self->conn);
    close(self->sockfd);
    Py_RETURN_NONE;
}

static PyObject *PyRabbitMQ_Connection_channel_open(PyRabbitMQ_Connection *self,
        PyObject *args) {
    int channel;
    if (PyArg_ParseTuple(args, "I", &channel)) {
        amqp_channel_open(self->conn, channel);
        amqp_get_rpc_reply(self->conn);
    }
    Py_RETURN_NONE;
}

static PyObject *PyRabbitMQ_Connection_channel_close(PyRabbitMQ_Connection *self,
        PyObject *args) {
    int channel;
    if (PyArg_ParseTuple(args, "I", &channel)) {
        amqp_channel_close(self->conn, channel, AMQP_REPLY_SUCCESS);
    }
    Py_RETURN_NONE;
}


static PyObject *PyRabbitMQ_Connection_basic_publish(PyRabbitMQ_Connection *self,
        PyObject *args, PyObject *kwargs) {
    int channel = 0;
    char *exchange = NULL;
    char *routing_key = NULL;
    int mandatory = 0;
    int immediate = 0;
    char *message = 0;

    static char *kwlist[] = {"exchange", "routing_key", "message",
                             "channel", "mandatory", "immediate", NULL};
    if (PyArg_ParseTupleAndKeywords(args, kwargs, "sss|iii", kwlist,
                &exchange, &routing_key, &message, &channel, &mandatory,
                &immediate)) {
        amqp_basic_publish(self->conn, channel,
                           amqp_cstring_bytes(exchange),
                           amqp_cstring_bytes(routing_key),
                           (amqp_boolean_t)mandatory,
                           (amqp_boolean_t)immediate,
                           NULL,
                           (amqp_bytes_t){.len = sizeof(message), .bytes=message});
    }

    Py_RETURN_NONE;
}


static PyMethodDef PyRabbitMQ_functions[] = {
    {NULL, NULL, 0, NULL}
};

PyMODINIT_FUNC init_pyrabbitmq(void) {
    PyObject *module;

    if (PyType_Ready(&PyRabbitMQ_ConnectionType) < 0) {
        return;
    }

    module = Py_InitModule3("_pyrabbitmq", PyRabbitMQ_functions,
            "Hand-made wrapper for librabbitmq.");
    if (module == NULL) {
        return;
    }

    PyModule_AddStringConstant(module, "__version__", PYRABBITMQ_VERSION);

    Py_INCREF(&PyRabbitMQ_ConnectionType);
    PyModule_AddObject(module, "connection", (PyObject *)&PyRabbitMQ_ConnectionType);

    PyModule_AddIntConstant(module, "AMQP_SASL_METHOD_PLAIN", AMQP_SASL_METHOD_PLAIN);

}
