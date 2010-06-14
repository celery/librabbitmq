#include <stdlib.h>
#include <stdio.h>

#include <unistd.h>

#include "_rabbitmqmodule.h"
#include "pylibrabbitmq_distmeta.h"



/* handle_error */
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


/* handle_amqp_error */
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


/* Connection.__new__ */
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
        self->connected = 0;
    }

    return self;
}


/* Connection.dealloc */
static void PyRabbitMQ_ConnectionType_dealloc(PyRabbitMQ_Connection *self) {
    self->ob_type->tp_free(self);
}


/* Connection.__init__ */
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


/* Connection.connect */
static PyObject *PyRabbitMQ_Connection_connect(PyRabbitMQ_Connection *self) {
    amqp_rpc_reply_t reply;
    if (self->connected) {
        PyErr_SetString(PyRabbitMQExc_ConnectionError, "Already connected");
        goto error;
    }
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

    self->connected = 1;
    Py_RETURN_NONE;
error:
    return 0;

}


/* Connection.close */
static PyObject *PyRabbitMQ_Connection_close(PyRabbitMQ_Connection *self) {
    amqp_rpc_reply_t reply;
    if (self->connected) {
        reply = amqp_connection_close(self->conn, AMQP_REPLY_SUCCESS);
        if (!PyRabbitMQ_handle_amqp_error(reply, "Couldn't close connection",
                PyRabbitMQExc_ConnectionError))
            goto error;

        amqp_destroy_connection(self->conn);

        if (!PyRabbitMQ_handle_error(close(self->sockfd), "Couldn't close socket"))
            goto error;
        self->connected = 0;
    }

    Py_RETURN_NONE;
error:
    return 0;
}


/* Connection._channel_open */
static PyObject *PyRabbitMQ_Connection_channel_open(PyRabbitMQ_Connection *self,
        PyObject *args) {
    int channel;
    amqp_rpc_reply_t reply;

    if (!self->connected) {
        PyErr_SetString(PyRabbitMQExc_ConnectionError,
            "Open channel on closed connection.");
        return 0;
    }

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


/* Connection._channel_close */
static PyObject *PyRabbitMQ_Connection_channel_close(PyRabbitMQ_Connection *self,
        PyObject *args) {
    int channel;
    amqp_rpc_reply_t reply;

    if (!self->connected) {
        PyErr_SetString(PyRabbitMQExc_ConnectionError,
            "Can't close channel on closed connection.");
        return 0;
    }

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


/* basic_properties_to_PyDict */
void basic_properties_to_PyDict(amqp_basic_properties_t *props,
        PyObject *p) {

    if (props->_flags & AMQP_BASIC_CONTENT_TYPE_FLAG) {
        PyDict_SetItemString(p, "content_type",
            PyString_FromStringAndSize(props->content_type.bytes,
                props->content_type.len));
    }
    if (props->_flags & AMQP_BASIC_CONTENT_ENCODING_FLAG) {
        PyDict_SetItemString(p, "content_encoding",
            PyString_FromStringAndSize(props->content_encoding.bytes,
                props->content_encoding.len));
    }
    if (props->_flags & AMQP_BASIC_CORRELATION_ID_FLAG) {
        PyDict_SetItemString(p, "correlation_id",
            PyString_FromStringAndSize(props->correlation_id.bytes,
                props->correlation_id.len));
    }
    if (props->_flags & AMQP_BASIC_REPLY_TO_FLAG) {
        PyDict_SetItemString(p, "reply_to",
            PyString_FromStringAndSize(props->reply_to.bytes,
                props->reply_to.len));
    }
    if (props->_flags & AMQP_BASIC_EXPIRATION_FLAG) {
        PyDict_SetItemString(p, "expiration",
            PyString_FromStringAndSize(props->expiration.bytes,
                props->expiration.len));
    }
    if (props->_flags & AMQP_BASIC_MESSAGE_ID_FLAG) {
        PyDict_SetItemString(p, "message_id",
            PyString_FromStringAndSize(props->message_id.bytes,
                props->message_id.len));
    }
    if (props->_flags & AMQP_BASIC_TYPE_FLAG) {
        PyDict_SetItemString(p, "type",
            PyString_FromStringAndSize(props->type.bytes,
                props->type.len));
    }
    if (props->_flags & AMQP_BASIC_USER_ID_FLAG) {
        PyDict_SetItemString(p, "user_id",
            PyString_FromStringAndSize(props->user_id.bytes,
                props->user_id.len));
    }
    if (props->_flags & AMQP_BASIC_APP_ID_FLAG) {
        PyDict_SetItemString(p, "app_id",
            PyString_FromStringAndSize(props->app_id.bytes,
                props->app_id.len));
    }
    if (props->_flags & AMQP_BASIC_DELIVERY_MODE_FLAG) {
        PyDict_SetItemString(p, "delivery_mode",
            PyInt_FromLong(props->delivery_mode));
    }
    if (props->_flags & AMQP_BASIC_PRIORITY_FLAG) {
        PyDict_SetItemString(p, "priority",
            PyInt_FromLong(props->priority));
    }
    if (props->_flags & AMQP_BASIC_TIMESTAMP_FLAG) {
        PyDict_SetItemString(p, "timestamp",
            PyInt_FromLong(props->timestamp));
    }
}


/* PyDict_to_basic_properties */
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
    if ((value = PyDict_GetItemString(p, "correlation_id")) != NULL) {
        props->correlation_id = amqp_cstring_bytes(PyString_AsString(value));
        props->_flags |= AMQP_BASIC_CORRELATION_ID_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "reply_to")) != NULL) {
        props->reply_to = amqp_cstring_bytes(PyString_AsString(value));
        props->_flags |= AMQP_BASIC_REPLY_TO_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "expiration")) != NULL) {
        props->expiration = amqp_cstring_bytes(PyString_AsString(value));
        props->_flags |= AMQP_BASIC_EXPIRATION_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "message_id")) != NULL) {
        props->message_id = amqp_cstring_bytes(PyString_AsString(value));
        props->_flags |= AMQP_BASIC_MESSAGE_ID_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "type")) != NULL) {
        props->type = amqp_cstring_bytes(PyString_AsString(value));
        props->_flags |= AMQP_BASIC_TYPE_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "user_id")) != NULL) {
        props->user_id = amqp_cstring_bytes(PyString_AsString(value));
        props->_flags |= AMQP_BASIC_USER_ID_FLAG;
    }
    if ((value = PyDict_GetItemString(p, "app_id")) != NULL) {
        props->app_id = amqp_cstring_bytes(PyString_AsString(value));
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
        props->timestamp = (uint8_t)PyInt_AS_LONG(value);
        props->_flags |= AMQP_BASIC_TIMESTAMP_FLAG;
    }

    Py_XDECREF(value);
}


/* recv */
int PyRabbitMQ_recv(PyObject *p, amqp_connection_state_t conn, int piggyback) {
    amqp_frame_t frame;
    amqp_basic_deliver_t *deliver;
    amqp_basic_properties_t *props;
    size_t body_target;
    size_t body_received;
    int retval = 0;

    while (1) {
        PyObject *payload;
        PyObject *propdict;

        if (!piggyback) {
            PyObject *delivery_info;

            Py_BEGIN_ALLOW_THREADS;
            amqp_maybe_release_buffers(conn);
            retval = amqp_simple_wait_frame(conn, &frame);
            Py_END_ALLOW_THREADS;
            if (retval <= 0) break;
            if (frame.frame_type != AMQP_FRAME_METHOD) continue;
            if (frame.payload.method.id != AMQP_BASIC_DELIVER_METHOD) continue;

            delivery_info = PyDict_New();
            PyDict_SetItemString(p, "delivery_info", delivery_info);
            deliver = (amqp_basic_deliver_t *)frame.payload.method.decoded;
            PyDict_SetItemString(delivery_info, "delivery_tag",
                    PYINT_FROM_SSIZE_T((PY_SIZE_TYPE)deliver->delivery_tag));
            PyDict_SetItemString(delivery_info, "consumer_tag",
                PyString_FromStringAndSize(deliver->consumer_tag.bytes,
                    deliver->consumer_tag.len));
            PyDict_SetItemString(delivery_info, "exchange",
                PyString_FromStringAndSize(deliver->exchange.bytes,
                    deliver->exchange.len));
            PyDict_SetItemString(delivery_info, "routing_key",
                PyString_FromStringAndSize(deliver->routing_key.bytes,
                    deliver->routing_key.len));
            piggyback = 0;
        }

        Py_BEGIN_ALLOW_THREADS;
        retval = amqp_simple_wait_frame(conn, &frame);
        Py_END_ALLOW_THREADS;
        if (retval <= 0) break;

        if (frame.frame_type != AMQP_FRAME_HEADER) {
            char errorstr[1024];
            snprintf(errorstr, sizeof(errorstr),
                "Unexpected header %d", frame.frame_type);
            PyErr_SetString(PyRabbitMQExc_ChannelError,
                    errorstr);
            return -1;

        }

        PyDict_SetItemString(p, "channel",
                PyInt_FromLong((long)frame.channel));

        propdict = PyDict_New();
        PyDict_SetItemString(p, "properties", propdict);
        props = (amqp_basic_properties_t *)frame.payload.properties.decoded;
        basic_properties_to_PyDict(props, propdict);

        body_target = frame.payload.properties.body_size;
        body_received = 0;
        payload = PyString_FromStringAndSize("", 0);

        while (body_received < body_target) {
            PyObject *part;
            Py_BEGIN_ALLOW_THREADS;
            retval = amqp_simple_wait_frame(conn, &frame);
            Py_END_ALLOW_THREADS;
            if (retval <= 0) break;

            if (frame.frame_type != AMQP_FRAME_BODY) {
                PyErr_SetString(PyRabbitMQExc_ChannelError,
                    "Expected body, got unexpected frame");
            }

            body_received += frame.payload.body_fragment.len;
            part = PyString_FromStringAndSize(
                    frame.payload.body_fragment.bytes,
                    frame.payload.body_fragment.len);
            PyString_Concat(&payload, part);
            Py_DECREF(part);
            if (payload == NULL) return -1;
        }

        PyDict_SetItemString(p, "body", payload);
        break;
    }
    return retval;

}


/* Connection._queue_bind */
static PyObject *PyRabbitMQ_Connection_queue_bind(PyRabbitMQ_Connection *self,
        PyObject *args, PyObject *kwargs) {
    char *queue = NULL;
    char *exchange = NULL;
    char *routing_key = NULL;
    int channel;
    amqp_table_t arguments = AMQP_EMPTY_TABLE;
    amqp_rpc_reply_t reply;

    static char *kwlist[] = {"queue", "exchange", "routing_key", "channel", NULL};
    if (PyArg_ParseTupleAndKeywords(args, kwargs, "sssi", kwlist,
                &queue, &exchange, &routing_key, &channel)) {

        Py_BEGIN_ALLOW_THREADS;
        amqp_queue_bind(self->conn, channel,
                           amqp_cstring_bytes(queue),
                           amqp_cstring_bytes(exchange),
                           amqp_cstring_bytes(routing_key),
                           arguments);
        reply = amqp_get_rpc_reply(self->conn);
        Py_END_ALLOW_THREADS;

        if (!PyRabbitMQ_handle_amqp_error(reply,
                    "Queue bind", PyRabbitMQExc_ChannelError))
            goto error;
    }
    else {
        goto error;
    }

    Py_RETURN_NONE;

error:
    return 0;
}


/* Connection._queue_unbind */
static PyObject *PyRabbitMQ_Connection_queue_unbind(PyRabbitMQ_Connection *self,
        PyObject *args, PyObject *kwargs) {
    char *queue = NULL;
    char *exchange = NULL;
    char *binding_key = NULL;
    int channel;
    amqp_table_t arguments = AMQP_EMPTY_TABLE;
    amqp_rpc_reply_t reply;

    static char *kwlist[] = {"queue", "exchange", "binding_key", "channel", NULL};
    if (PyArg_ParseTupleAndKeywords(args, kwargs, "sssi", kwlist,
                &queue, &exchange, &binding_key, &channel)) {

        Py_BEGIN_ALLOW_THREADS;
        amqp_queue_unbind(self->conn, channel,
                           amqp_cstring_bytes(queue),
                           amqp_cstring_bytes(exchange),
                           amqp_cstring_bytes(binding_key),
                           arguments);
        reply = amqp_get_rpc_reply(self->conn);
        Py_END_ALLOW_THREADS;

        if (!PyRabbitMQ_handle_amqp_error(reply,
                    "Queue unbind", PyRabbitMQExc_ChannelError))
            goto error;
    }
    else {
        goto error;
    }

    Py_RETURN_NONE;

error:
    return 0;
}


/* Connection._queue_declare */
static PyObject *PyRabbitMQ_Connection_queue_declare(PyRabbitMQ_Connection *self,
        PyObject *args, PyObject *kwargs) {
    char *queue = NULL;
    int channel, passive, durable, exclusive, auto_delete;
    amqp_queue_declare_ok_t *ok;
    amqp_rpc_reply_t reply;

    static char *kwlist[] = {"queue", "channel", "passive", "durable",
                             "exclusive", "auto_delete", NULL};
    if (PyArg_ParseTupleAndKeywords(args, kwargs, "siiiii", kwlist,
                &queue, &channel, &passive, &durable, &exclusive, &auto_delete)) {

        Py_BEGIN_ALLOW_THREADS;
        ok = amqp_queue_declare(self->conn, channel,
                           amqp_cstring_bytes(queue),
                           (amqp_boolean_t)passive,
                           (amqp_boolean_t)durable,
                           (amqp_boolean_t)exclusive,
                           (amqp_boolean_t)auto_delete,
                           AMQP_EMPTY_TABLE);
        reply = amqp_get_rpc_reply(self->conn);
        Py_END_ALLOW_THREADS;

        if (!PyRabbitMQ_handle_amqp_error(reply,
                    "queue.declare", PyRabbitMQExc_ChannelError))
            goto error;

        PyObject *p = PyDict_New();
        PyDict_SetItemString(p, "message_count",
                PyInt_FromLong((long)ok->message_count));
        PyDict_SetItemString(p, "consumer_count",
                PyInt_FromLong((long)ok->consumer_count));
        PyDict_SetItemString(p, "queue",
                PyString_FromStringAndSize(ok->queue.bytes,
                    ok->queue.len));
        return p;
    }
    else {
        goto error;
    }

    Py_RETURN_NONE;

error:
    return 0;
}


/* Connection._queue_purge */
static PyObject *PyRabbitMQ_Connection_queue_purge(PyRabbitMQ_Connection *self,
        PyObject *args, PyObject *kwargs) {
    char *queue = NULL;
    int channel, no_wait;
    amqp_queue_purge_ok_t *ok;
    amqp_rpc_reply_t reply;

    static char *kwlist[] = {"queue", "no_wait", "channel", NULL};
    if (PyArg_ParseTupleAndKeywords(args, kwargs, "sii", kwlist,
                &queue, &no_wait, &channel)) {

        Py_BEGIN_ALLOW_THREADS;
        ok = amqp_queue_purge(self->conn, channel,
                           amqp_cstring_bytes(queue),
                           (amqp_boolean_t)no_wait);
        reply = amqp_get_rpc_reply(self->conn);
        Py_END_ALLOW_THREADS;

        if (!PyRabbitMQ_handle_amqp_error(reply,
                    "queue.purge", PyRabbitMQExc_ChannelError))
            goto error;

        return PyInt_FromLong((long)ok->message_count);
    }
    else {
        goto error;
    }

    Py_RETURN_NONE;

error:
    return 0;
}


/* Connection._exchange_declare */
static PyObject *PyRabbitMQ_Connection_exchange_declare(PyRabbitMQ_Connection *self,
        PyObject *args, PyObject *kwargs) {
    char *exchange = NULL;
    char *type = NULL;
    int channel, passive, durable, auto_delete;
    amqp_table_t arguments = AMQP_EMPTY_TABLE;
    amqp_rpc_reply_t reply;

    static char *kwlist[] = {"exchange", "type",
                             "channel", "passive", "durable",
                             "auto_delete", NULL}; /* TODO arguments */
    if (PyArg_ParseTupleAndKeywords(args, kwargs, "ssiiii", kwlist,
                &exchange, &type, &channel, &passive,
                &durable, &auto_delete)) {

        Py_BEGIN_ALLOW_THREADS;
        amqp_exchange_declare(self->conn, channel,
                           amqp_cstring_bytes(exchange),
                           amqp_cstring_bytes(type),
                           (amqp_boolean_t)passive,
                           (amqp_boolean_t)durable,
                           (amqp_boolean_t)auto_delete,
                           arguments);
        reply = amqp_get_rpc_reply(self->conn);
        Py_END_ALLOW_THREADS;

        if (!PyRabbitMQ_handle_amqp_error(reply,
                    "Exchange declare", PyRabbitMQExc_ChannelError))
            goto error;
    }
    else {
        goto error;
    }

    Py_RETURN_NONE;

error:
    return 0;
}


/* Connection._basic_publish */
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

        Py_BEGIN_ALLOW_THREADS;
        ret = amqp_basic_publish(self->conn, channel,
                           amqp_cstring_bytes(exchange),
                           amqp_cstring_bytes(routing_key),
                           (amqp_boolean_t)mandatory,
                           (amqp_boolean_t)immediate,
                           &props,
                           (amqp_bytes_t){.len = message_size, .bytes=message});
        Py_END_ALLOW_THREADS;


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


/* Connection._basic_ack */
static PyObject *PyRabbitMQ_Connection_basic_ack(PyRabbitMQ_Connection *self,
        PyObject *args, PyObject *kwargs) {
    int channel = 0;
    int multiple = 0;
    PyObject *delivery_tag = NULL;
    PY_SIZE_TYPE tag;
    int ret;

    static char *kwlist[] = {"delivery_tag", "multiple", "channel", NULL};
    if (PyArg_ParseTupleAndKeywords(args, kwargs, "Oii", kwlist,
                &delivery_tag, &multiple, &channel)) {

        tag = PYINT_AS_SSIZE_T(delivery_tag);
        if (tag == -1 && PyErr_Occurred() != NULL)
            goto error;

        Py_BEGIN_ALLOW_THREADS;
        ret = amqp_basic_ack(self->conn, channel,
                           (uint64_t)tag,
                           (amqp_boolean_t)multiple);

        Py_END_ALLOW_THREADS;

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


/* Connection._basic_consume */
static PyObject *PyRabbitMQ_Connection_basic_consume(PyRabbitMQ_Connection *self,
        PyObject *args, PyObject *kwargs) {
    char *queue = NULL;
    char *consumer_tag = NULL;
    int channel, no_local, no_ack, exclusive;
    amqp_basic_consume_ok_t *ok;
    amqp_rpc_reply_t reply;

    static char *kwlist[] = {"queue", "consumer_tag", "no_local",
                             "no_ack", "exclusive", "channel", NULL};
    if (PyArg_ParseTupleAndKeywords(args, kwargs, "ssiiii", kwlist,
                &queue, &consumer_tag, &no_local, &no_ack,
                &exclusive, &channel)) {

        Py_BEGIN_ALLOW_THREADS;
        ok = amqp_basic_consume(self->conn, channel,
                           amqp_cstring_bytes(queue),
                           amqp_cstring_bytes(consumer_tag),
                           no_local,
                           no_ack,
                           exclusive);
        reply = amqp_get_rpc_reply(self->conn);
        Py_END_ALLOW_THREADS;

        if (!PyRabbitMQ_handle_amqp_error(reply,
                    "basic.consume", PyRabbitMQExc_ChannelError))
            goto error;

        return PyString_FromStringAndSize(ok->consumer_tag.bytes,
                                          ok->consumer_tag.len);
    }
    else {
        goto error;
    }

    Py_RETURN_NONE;

error:
    return 0;
}

/* Connection._basic_recv */
static PyObject *PyRabbitMQ_Connection_basic_recv(PyRabbitMQ_Connection *self,
        PyObject *args, PyObject *kwargs) {
    int retval;
    PyObject *p;

    p = PyDict_New();
    retval = PyRabbitMQ_recv(p, self->conn, 0);
    if (retval < 0) {
        if (PyErr_Occurred() == NULL) {
            PyErr_SetString(PyRabbitMQExc_ChannelError,
                    "Bad frame read");
        }
        Py_XDECREF(p);
        goto error;
    }
    return p;

error:
    return 0;
}


/* Connection._basic_get */
static PyObject *PyRabbitMQ_Connection_basic_get(PyRabbitMQ_Connection *self,
        PyObject *args, PyObject *kwargs) {
    char *queue = NULL;
    int noack = 0;
    int channel = 0;
    amqp_rpc_reply_t reply;

    static char *kwlist[] = {"queue", "noack", "channel", NULL};
    if (PyArg_ParseTupleAndKeywords(args, kwargs, "sii", kwlist,
                &queue, &noack, &channel)) {

        Py_BEGIN_ALLOW_THREADS;
        reply = amqp_basic_get(self->conn, channel,
                           amqp_cstring_bytes(queue),
                           noack);
        Py_END_ALLOW_THREADS;

        if (!PyRabbitMQ_handle_amqp_error(reply,
                    "basic.get", PyRabbitMQExc_ChannelError))
            goto error;
        if (reply.reply.id == AMQP_BASIC_GET_OK_METHOD) {
            amqp_basic_get_ok_t *ok = (amqp_basic_get_ok_t *)reply.reply.decoded;
            PyObject *p = PyDict_New();
            PyObject *delivery_info = PyDict_New();
            PyDict_SetItemString(p, "delivery_info", delivery_info);
            PyDict_SetItemString(delivery_info, "delivery_tag",
                    PYINT_FROM_SSIZE_T((PY_SIZE_TYPE)ok->delivery_tag));
            PyDict_SetItemString(delivery_info, "redelivered",
                    PyInt_FromLong((long)ok->redelivered));
            PyDict_SetItemString(delivery_info, "exchange",
                    PyString_FromStringAndSize(ok->exchange.bytes,
                        ok->exchange.len));
            PyDict_SetItemString(delivery_info, "routing_key",
                    PyString_FromStringAndSize(ok->routing_key.bytes,
                        ok->routing_key.len));
            PyDict_SetItemString(delivery_info, "message_count",
                    PyInt_FromLong((long)ok->message_count));

            if (amqp_data_in_buffer(self->conn)) {
                if (PyRabbitMQ_recv(p, self->conn, 1) < 0) {
                    if (PyErr_Occurred() == NULL) {
                        PyErr_SetString(PyRabbitMQExc_ChannelError,
                                "Bad frame read");
                    }
                    Py_XDECREF(p);
                    Py_XDECREF(delivery_info);
                    goto error;
                }
            }
            return p;
        }
    }
    else {
        goto error;
    }

    Py_RETURN_NONE;

error:
    return 0;
}

/* Module: _pyrabbitmq */

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
    PyModule_AddStringConstant(module, "__author__", PYRABBITMQ_AUTHOR);
    PyModule_AddStringConstant(module, "__contact__", PYRABBITMQ_CONTACT);
    PyModule_AddStringConstant(module, "__homepage__", PYRABBITMQ_HOMEPAGE);

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
