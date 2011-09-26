#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/time.h>

#include "_rabbitmqmodule.h"
#include "pylibrabbitmq_distmeta.h"

#define PyDICT_SETSTR_DECREF(dict, key, value, stmt)  \
    ({                                          \
        value = stmt;                           \
        PyDict_SetItemString(dict, key, value); \
        Py_DECREF(value);                       \
    })


/* latest librabbitmq does not support the auto_delete argument to
 * amqp_exchange_declare() */
amqp_exchange_declare_ok_t *_amqp2_exchange_declare(amqp_connection_state_t state, amqp_channel_t channel,
        amqp_bytes_t exchange, amqp_bytes_t type, amqp_boolean_t passive, amqp_boolean_t durable,
        amqp_boolean_t auto_delete, amqp_table_t arguments) {
  amqp_exchange_declare_t req;
  req.ticket = 0;
  req.exchange = exchange;
  req.type = type;
  req.passive = passive;
  req.durable = durable;
  req.auto_delete = auto_delete;
  req.internal = 0;
  req.nowait = 0;
  req.arguments = arguments;

  return amqp_simple_rpc_decoded(state, channel, AMQP_EXCHANGE_DECLARE_METHOD, AMQP_EXCHANGE_DECLARE_OK_METHOD, &req);
}



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
                    reply.library_error
                        ? strerror(reply.library_error)
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
        self->virtual_host = NULL;
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
    char *virtual_host;
    int channel_max = 0xffff;
    int frame_max = 131072;
    int heartbeat = 0;
    int port = 5672;

    static char *kwlist[] = {"hostname", "userid", "password",
                             "virtual_host", "port", "channel_max",
                             "frame_max", "heartbeat", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ssssiiii", kwlist,
                &hostname, &userid, &password, &virtual_host, &port,
                &channel_max, &frame_max, &heartbeat)) {
        return -1;
    }

    self->hostname = hostname;
    self->userid = userid;
    self->password = password;
    self->virtual_host = virtual_host;
    self->port = port;
    self->channel_max = channel_max;
    self->frame_max = frame_max;
    self->heartbeat = heartbeat;

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
    reply = amqp_login(self->conn, self->virtual_host, self->channel_max,
                       self->frame_max, self->heartbeat,
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

    PyObject *value = NULL;

    if (props->_flags & AMQP_BASIC_CONTENT_TYPE_FLAG) {
        PyDICT_SETSTR_DECREF(p, "content_type", value,
                PyString_FromStringAndSize(props->content_type.bytes,
                    props->content_type.len));
    }
    if (props->_flags & AMQP_BASIC_CONTENT_ENCODING_FLAG) {
        PyDICT_SETSTR_DECREF(p, "content_encoding", value,
            PyString_FromStringAndSize(props->content_encoding.bytes,
                props->content_encoding.len));
    }
    if (props->_flags & AMQP_BASIC_CORRELATION_ID_FLAG) {
        PyDICT_SETSTR_DECREF(p, "correlation_id", value,
            PyString_FromStringAndSize(props->correlation_id.bytes,
                props->correlation_id.len));
    }
    if (props->_flags & AMQP_BASIC_REPLY_TO_FLAG) {
        PyDICT_SETSTR_DECREF(p, "reply_to", value,
            PyString_FromStringAndSize(props->reply_to.bytes,
                    props->reply_to.len));
    }
    if (props->_flags & AMQP_BASIC_EXPIRATION_FLAG) {
        PyDICT_SETSTR_DECREF(p, "expiration", value,
            PyString_FromStringAndSize(props->expiration.bytes,
                props->expiration.len));
    }
    if (props->_flags & AMQP_BASIC_MESSAGE_ID_FLAG) {
        PyDICT_SETSTR_DECREF(p, "message_id", value,
            PyString_FromStringAndSize(props->message_id.bytes,
                props->message_id.len));
    }
    if (props->_flags & AMQP_BASIC_TYPE_FLAG) {
        PyDICT_SETSTR_DECREF(p, "type", value,
            PyString_FromStringAndSize(props->type.bytes,
                props->type.len));
    }
    if (props->_flags & AMQP_BASIC_USER_ID_FLAG) {
        PyDICT_SETSTR_DECREF(p, "user_id", value,
            PyString_FromStringAndSize(props->user_id.bytes,
               props->user_id.len));
    }
    if (props->_flags & AMQP_BASIC_APP_ID_FLAG) {
        PyDICT_SETSTR_DECREF(p, "app_id", value,
            PyString_FromStringAndSize(props->app_id.bytes,
                props->app_id.len));
    }
    if (props->_flags & AMQP_BASIC_DELIVERY_MODE_FLAG) {
        PyDICT_SETSTR_DECREF(p, "delivery_mode", value,
            PyInt_FromLong(props->delivery_mode));
    }
    if (props->_flags & AMQP_BASIC_PRIORITY_FLAG) {
        PyDICT_SETSTR_DECREF(p, "priority", value,
            PyInt_FromLong(props->priority));
    }
    if (props->_flags & AMQP_BASIC_TIMESTAMP_FLAG) {
        PyDICT_SETSTR_DECREF(p, "timestamp", value,
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
    memset(&props, 0, sizeof(props));

    while (1) {
        PyObject *payload = NULL;
        PyObject *propdict = NULL;
        PyObject *value = NULL;

        if (!piggyback) {
            PyObject *delivery_info = NULL;

            Py_BEGIN_ALLOW_THREADS;
            amqp_maybe_release_buffers(conn);
            retval = amqp_simple_wait_frame(conn, &frame);
            Py_END_ALLOW_THREADS;
            if (retval < 0) break;
            if (frame.frame_type != AMQP_FRAME_METHOD
                    || frame.payload.method.id != AMQP_BASIC_DELIVER_METHOD)
                continue;


            deliver = (amqp_basic_deliver_t *)frame.payload.method.decoded;
            // p["delivery_info"] = {}
            PyDICT_SETSTR_DECREF(p, "delivery_info", delivery_info,
                    PyDict_New());

            // p["delivery_info"]["delivery_tag"]
            PyDICT_SETSTR_DECREF(delivery_info, "delivery_tag", value,
                PYINT_FROM_SSIZE_T((PY_SIZE_TYPE)deliver->delivery_tag));

            // p["delivery_info"]["consumer_tag"]
            PyDICT_SETSTR_DECREF(delivery_info, "consumer_tag", value,
                PyString_FromStringAndSize(deliver->consumer_tag.bytes,
                    deliver->consumer_tag.len));

            // p["delivery_info"]["exchange"]
            PyDICT_SETSTR_DECREF(delivery_info, "exchange", value,
                PyString_FromStringAndSize(deliver->exchange.bytes,
                    deliver->exchange.len));

            // p["delivery_info"]["routing_key"]
            PyDICT_SETSTR_DECREF(delivery_info, "routing_key", value,
                PyString_FromStringAndSize(deliver->routing_key.bytes,
                    deliver->routing_key.len));

            piggyback = 0;
        }

        Py_BEGIN_ALLOW_THREADS;
        retval = amqp_simple_wait_frame(conn, &frame);
        Py_END_ALLOW_THREADS;
        if (retval < 0) break;

        if (frame.frame_type != AMQP_FRAME_HEADER) {
            char errorstr[1024];
            snprintf(errorstr, sizeof(errorstr),
                "Unexpected header %d", frame.frame_type);
            PyErr_SetString(PyRabbitMQExc_ChannelError,
                    errorstr);
            return -1;

        }

        // p["channel"]
        PyDICT_SETSTR_DECREF(p, "channel", value,
                PyInt_FromLong((long)frame.channel));

        propdict = PyDict_New();

        // p["properties"] = {}
        props = (amqp_basic_properties_t *)frame.payload.properties.decoded;
        PyDICT_SETSTR_DECREF(p, "properties", propdict,
                PyDict_New());
        basic_properties_to_PyDict(props, propdict);

        // p["body"]
        body_target = frame.payload.properties.body_size;
        body_received = 0;
        payload = PyString_FromStringAndSize("", 0);
        while (body_received < body_target) {
            PyObject *part;
            Py_BEGIN_ALLOW_THREADS;
            retval = amqp_simple_wait_frame(conn, &frame);
            Py_END_ALLOW_THREADS;
            if (retval < 0) break;

            if (frame.frame_type != AMQP_FRAME_BODY) {
                PyErr_SetString(PyRabbitMQExc_ChannelError,
                    "Expected body, got unexpected frame");
                return -1;
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
        Py_DECREF(payload);
        break;
    }
    amqp_maybe_release_buffers(conn);
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
        PyObject *value = NULL;
        PyDICT_SETSTR_DECREF(p, "message_count", value,
            PyInt_FromLong((long)ok->message_count));
        PyDICT_SETSTR_DECREF(p, "consumer_count", value,
            PyInt_FromLong((long)ok->consumer_count));
        PyDICT_SETSTR_DECREF(p, "queue", value,
            PyString_FromStringAndSize(ok->queue.bytes, ok->queue.len));
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

    static char *kwlist[] = {"queue", "nowait", "channel", NULL};
    if (PyArg_ParseTupleAndKeywords(args, kwargs, "sii", kwlist,
                &queue, &no_wait, &channel)) {

        Py_BEGIN_ALLOW_THREADS;
        ok = amqp_queue_purge(self->conn, channel,
                           amqp_cstring_bytes(queue));
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
        _amqp2_exchange_declare(self->conn, channel,
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
    char *body = 0;
    PY_SIZE_TYPE body_size;
    amqp_basic_properties_t props;
    PyObject *propdict;

    static char *kwlist[] = {"channel", "body", "exchange", "routing_key",
                             "properties", "mandatory", "immediate", NULL};
    if (PyArg_ParseTupleAndKeywords(args, kwargs, "is#ssO|ii", kwlist,
                &channel, &body, &body_size, &exchange, &routing_key,
                &propdict, &mandatory, &immediate)) {

        memset(&props, 0, sizeof(props));
        PyDict_to_basic_properties(propdict, &props);

        Py_BEGIN_ALLOW_THREADS;
        ret = amqp_basic_publish(self->conn, channel,
                           amqp_cstring_bytes(exchange),
                           amqp_cstring_bytes(routing_key),
                           (amqp_boolean_t)mandatory,
                           (amqp_boolean_t)immediate,
                           &props,
                           (amqp_bytes_t){.len = body_size, .bytes=body});
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

        if (!PyRabbitMQ_handle_error(ret, "Basic Ack"))
            goto error;
    }
    else {
        goto error;
    }

    Py_RETURN_NONE;

error:
    return 0;
}

/* Connection._basic_reject */
static PyObject *PyRabbitMQ_Connection_basic_reject(PyRabbitMQ_Connection *self,
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
        ret = amqp_basic_reject(self->conn, channel,
                           (uint64_t)tag,
                           (amqp_boolean_t)multiple);
        Py_END_ALLOW_THREADS;

        if (!PyRabbitMQ_handle_error(ret, "Basic Nack"))
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
    amqp_table_t arguments = AMQP_EMPTY_TABLE;

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
                           exclusive,
                           arguments);
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

/* Connection._basic_qos */
static PyObject *PyRabbitMQ_Connection_basic_qos(PyRabbitMQ_Connection *self,
        PyObject *args, PyObject *kwargs) {
    int channel = 0;
    int prefetch_size;
    short prefetch_count;
    int _global = 0;
    int ret;

    static char *kwlist[] = {"prefetch_size", "prefetch_count", "_global", "channel", NULL};
    if (PyArg_ParseTupleAndKeywords(args, kwargs, "iiii", kwlist, &prefetch_size,
                &prefetch_count, &_global, &channel)) {

        Py_BEGIN_ALLOW_THREADS;
        ret = amqp_basic_qos(self->conn, channel,
                           (uint32_t)prefetch_size,
                           (uint16_t)prefetch_count,
                           (amqp_boolean_t)_global);
        Py_END_ALLOW_THREADS;

        if (!PyRabbitMQ_handle_error(ret, "Basic Qos"))
            goto error;

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
    int ready = 0;
    double timeout;
    amqp_boolean_t buffered;
    PyObject *p;

    static char *kwlist[] = {"timeout", NULL};
    if (PyArg_ParseTupleAndKeywords(args, kwargs, "d", kwlist, &timeout)) {
        buffered = amqp_data_in_buffer(self->conn);

        if (timeout > 0.0 && !buffered) {
            ready = PyRabbitMQ_wait_timeout(self->sockfd, timeout);
            if (ready == 0) {
                if (PyErr_Occurred() == NULL) {
                    PyErr_SetString(PyRabbitMQExc_TimeoutError, "timed out");
                }
                goto error;
            }
            else if (ready < 0) {
                if (PyErr_Occurred() == NULL) {
                    PyErr_SetFromErrno(PyExc_OSError);
                }
                goto error;
            }
        }

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
    }
    else {
        goto error;
    }

error:
    return 0;
}


static long long PyRabbitMQ_now_usec(void) {
   struct timeval tv;
   gettimeofday(&tv, NULL);
   return (long long)tv.tv_sec * 1000000 + (long long)tv.tv_usec;
 }


static int PyRabbitMQ_wait_timeout(int sockfd, double timeout) {
    long long t1, t2;
    int result = 0;
    fd_set fdset;
    struct timeval tv;

    while (timeout > 0.0) {
        FD_ZERO(&fdset);
        FD_SET(sockfd, &fdset);
        tv.tv_sec = (int)timeout;
        tv.tv_usec = (int)((timeout - tv.tv_sec) * 1e6);

        t1 = PyRabbitMQ_now_usec();
        Py_BEGIN_ALLOW_THREADS;
        result = select(sockfd + 1, &fdset, NULL, NULL, &tv);
        Py_END_ALLOW_THREADS;

        if (result <= 0)
            break;

        if (FD_ISSET(sockfd, &fdset)) {
            result = 1;
            break;
        }

        t2 = PyRabbitMQ_now_usec();
        timeout -= (double)(t2 / 1e6) - (t1 / 1e6);
    }
    return result;
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
            PyObject *delivery_info = NULL;
            PyObject *value = NULL;

            // p["delivery_info"] = {}
            PyDICT_SETSTR_DECREF(p, "delivery_info", delivery_info,
                    PyDict_New());

            // p["delivery_info"]["delivery_tag"]
            PyDICT_SETSTR_DECREF(delivery_info, "delivery_tag", value,
                PYINT_FROM_SSIZE_T((PY_SIZE_TYPE)ok->delivery_tag));

            // p["delivery_info"]["redelivered"]
            PyDICT_SETSTR_DECREF(delivery_info, "redelivered", value,
                PyInt_FromLong((long)ok->redelivered));

            // p["delivery_info"]["exchange"]
            PyDICT_SETSTR_DECREF(delivery_info, "exchange", value,
                PyString_FromStringAndSize(ok->exchange.bytes,
                    ok->exchange.len));

            // p["delivery_info"]["routing_key"]
            PyDICT_SETSTR_DECREF(delivery_info, "routing_key", value,
                PyString_FromStringAndSize(ok->routing_key.bytes,
                    ok->routing_key.len));

            // p["delivery_info"]["message_count"]
            PyDICT_SETSTR_DECREF(delivery_info, "message_count", value,
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
    PyRabbitMQExc_TimeoutError = PyErr_NewException(
            "_pyrabbitmq.TimeoutError", NULL, NULL);
    PyModule_AddObject(module, "TimeoutError",
                       (PyObject *)PyRabbitMQExc_TimeoutError);
    PyModule_AddObject(module, "ChannelError",
                       (PyObject *)PyRabbitMQExc_ChannelError);
}
