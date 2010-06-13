#include <stdlib.h>
#include <stdio.h>

#include <unistd.h>

#include "_rabbitmqmodule.h"

#define PYRABBITMQ_VERSION "0.0.1"

int PyRabbitMQ_handle_error(int ret, char const *context) {
    if (ret < 0) {
        char errorstr[1024];
        snprintf(errorstr, sizeof(errorstr), "%s: %s", 
                context, strerror(-ret));
        PyErr_SetString(PyRabbitMQExc_ConnectionError, errorstr);
        return 0;
    }
    return 1;
}

int PyRabbitMQ_handle_amqp_error(amqp_rpc_reply_t reply, char const *context,
        PyObject *exc_cls) {
    char errorstr[1024];

    switch (reply.reply_type) {
        case AMQP_RESPONSE_NORMAL:
            return 1;

        case AMQP_RESPONSE_NONE:
            snprintf(errorstr, sizeof(errorstr),
                    "%s: missing RPC reply type!", context);
            break;

        case AMQP_RESPONSE_LIBRARY_EXCEPTION:
            snprintf(errorstr, sizeof(errorstr), "%s: %s",
                    context,
                    reply.library_errno
                        ? strerror(reply.library_errno)
                        : "(end-of-stream)");
            break;

        case AMQP_RESPONSE_SERVER_EXCEPTION:

            switch (reply.reply.id) {
                case AMQP_CONNECTION_CLOSE_METHOD: {
                    amqp_connection_close_t *m = (amqp_connection_close_t *) reply.reply.decoded;
                    snprintf(errorstr, sizeof(errorstr),
                        "%s: server connection error %d, message: %.*s",
                            context,
                            m->reply_code,
                            (int) m->reply_text.len, (char *) m->reply_text.bytes);
                    break;
                }
                case AMQP_CHANNEL_CLOSE_METHOD: {
                    amqp_channel_close_t *m = (amqp_channel_close_t *) reply.reply.decoded;
                    snprintf(errorstr, sizeof(errorstr),
                        "%s: server channel error %d, message: %.*s",
                            context, m->reply_code,
                            (int) m->reply_text.len, (char *) m->reply_text.bytes);
                    break;
                }
                default:
                    snprintf(errorstr, sizeof(errorstr),
                        "%s: unknown server error, method id 0x%08X",
                            context, reply.reply.id);
                    break;
            }
            break;
    }
    PyErr_SetString(exc_cls, errorstr);
    return 0;
}


/* __new__ */
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

/* dealloc */
static void PyRabbitMQ_ConnectionType_dealloc(PyRabbitMQ_Connection *self) {
    self->ob_type->tp_free(self);
}

/* __init__ */
static int PyRabbitMQ_Connection_init(PyRabbitMQ_Connection *self,
        PyObject *args, PyObject *kwargs) {
    char *hostname;
    char *userid;
    char *password;
    char *vhost;
    int port;

    static char *kwlist[] = {"hostname", "userid", "password",
                             "vhost", "port", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|ssssi", kwlist,
                &hostname, &userid, &password, &vhost, &port)) {
        return -1;
    }

    self->hostname = hostname;
    self->userid = userid;
    self->password = password;
    self->vhost = vhost;
    self->port = port;

    return 0;
}

/* connect */
static PyObject *PyRabbitMQ_Connection_connect(PyRabbitMQ_Connection *self) {
    amqp_rpc_reply_t reply;
    self->conn = amqp_new_connection();
    self->sockfd = amqp_open_socket(self->hostname, self->port);
    if (!PyRabbitMQ_handle_error(self->sockfd, "Couldn't open socket"))
        goto error;
    amqp_set_sockfd(self->conn, self->sockfd);
    reply = amqp_login(self->conn, self->vhost, 0, 131072, 0,
                       AMQP_SASL_METHOD_PLAIN, self->userid, self->password);
    if (!PyRabbitMQ_handle_amqp_error(reply, "Couldn't log in",
            PyRabbitMQExc_ConnectionError))
        goto error;

    Py_RETURN_NONE;
error:
    return 0;

}

/* close */
static PyObject *PyRabbitMQ_Connection_close(PyRabbitMQ_Connection *self) {
    amqp_rpc_reply_t reply;

    reply = amqp_connection_close(self->conn, AMQP_REPLY_SUCCESS);
    if (!PyRabbitMQ_handle_amqp_error(reply, "Couldn't close connection",
            PyRabbitMQExc_ConnectionError))
        goto error;

    amqp_destroy_connection(self->conn);

    if (!PyRabbitMQ_handle_error(close(self->sockfd), "Couldn't close socket"))
        goto error;

    Py_RETURN_NONE;
error:
    return 0;
}

/* channel_open */
static PyObject *PyRabbitMQ_Connection_channel_open(PyRabbitMQ_Connection *self,
        PyObject *args) {
    int channel;
    amqp_rpc_reply_t reply;

    if (PyArg_ParseTuple(args, "I", &channel)) {
        amqp_channel_open(self->conn, channel);

        reply = amqp_get_rpc_reply(self->conn);
        if (!PyRabbitMQ_handle_amqp_error(reply, "Couldn't create channel",
                PyRabbitMQExc_ChannelError))
            goto error;
    }

    Py_RETURN_NONE;

error:
    return 0;
}

/* channel_close */
static PyObject *PyRabbitMQ_Connection_channel_close(PyRabbitMQ_Connection *self,
        PyObject *args) {
    int channel;
    amqp_rpc_reply_t reply;

    if (PyArg_ParseTuple(args, "I", &channel)) {
        reply = amqp_channel_close(self->conn, channel, AMQP_REPLY_SUCCESS);
        if (!PyRabbitMQ_handle_amqp_error(reply, "Couldn't close channel",
                PyRabbitMQExc_ChannelError))
            goto error;
    }

    Py_RETURN_NONE;

error:
    return 0;
}

void PyDict_to_basic_properties(PyObject *p, amqp_basic_properties_t *props) {
    props->_flags = 0;
    PyObject *value = NULL;

    if ((value = PyDict_GetItemString(p, "content_type")) != NULL) {
        props->content_type = amqp_cstring_bytes(PyString_AsString(value));
        props->_flags |= AMQP_BASIC_CONTENT_TYPE_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "content_encoding")) != NULL) {
        props->content_encoding = amqp_cstring_bytes(PyString_AsString(value));
        props->_flags |= AMQP_BASIC_CONTENT_ENCODING_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "delivery_mode")) != NULL) {
        props->delivery_mode = (uint8_t)PyInt_AS_LONG(value);
        props->_flags |= AMQP_BASIC_DELIVERY_MODE_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "priority")) != NULL) {
        props->priority = (uint8_t)PyInt_AS_LONG(value);
        props->_flags |= AMQP_BASIC_PRIORITY_FLAG;
    }


}


/* basic_publish */
static PyObject *PyRabbitMQ_Connection_basic_publish(PyRabbitMQ_Connection *self,
        PyObject *args, PyObject *kwargs) {
    int ret;
    int channel = 0;
    char *exchange = NULL;
    char *routing_key = NULL;
    int mandatory = 0;
    int immediate = 0;
    char *message = 0;
    amqp_basic_properties_t props;
    PyObject *propdict;
    PY_SIZE_TYPE message_size;

    static char *kwlist[] = {"exchange", "routing_key", "message",
                             "channel", "mandatory", "immediate",
                             "properties", NULL};
    if (PyArg_ParseTupleAndKeywords(args, kwargs, "sss#iiiO", kwlist,
                &exchange, &routing_key, &message, &message_size,
                &channel, &mandatory, &immediate, &propdict)) {
        PyDict_to_basic_properties(propdict, &props);
        ret = amqp_basic_publish(self->conn, channel,
                           amqp_cstring_bytes(exchange),
                           amqp_cstring_bytes(routing_key),
                           (amqp_boolean_t)mandatory,
                           (amqp_boolean_t)immediate,
                           &props,
                           (amqp_bytes_t){.len = message_size, .bytes=message});
        if (!PyRabbitMQ_handle_error(ret, "Basic Publish"))
            goto error;
    }
    else {
        goto error;
    }

    Py_RETURN_NONE;

error:
    return 0;
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

    PyRabbitMQExc_ConnectionError = PyErr_NewException(
            "_pyrabbitmq.ConnectionError", NULL, NULL);
    PyModule_AddObject(module, "ConnectionError",
                       (PyObject *)PyRabbitMQExc_ConnectionError);
    PyRabbitMQExc_ChannelError = PyErr_NewException(
            "_pyrabbitmq.ChannelError", NULL, NULL);
    PyModule_AddObject(module, "ChannelError",
                       (PyObject *)PyRabbitMQExc_ChannelError);


}
