==================================================
 pylibrabbitmq - Python bindings to librabbitmq-c
==================================================

**EXPERIMENTAL!**


Examples
========

    >>> from pylibrabbitmq import Connection, Message

    >>> conn = Connection(hostname="localhost", port=5672, userid="guest",
    ...                   password="guest", vhost="/")
    >>> conn.connect()

    >>> channel = conn.channel()
    >>> channel.exchange_declare(exchange, type, ...)
    >>> channel.queue_declare(queue, ...)
    >>> channel.queue_bind(queue, exchange, routing_key)

Produce
-------

    >>> m = Message(body, content_type=None, content_encoding=None,
    ...             delivery_mode=1)
    >>> channel.basic_publish(m, exchange, routing_key, ...)

Consume
-------

    >>> def dump_message(message):
    ...     print("Body:'%s', Proeprties:'%s', DeliveryInfo:'%s'" % (
    ...         message.body, message.properties, message.delivery_info))
    ...     message.ack()

    >>> channel.basic_consume(queue, ..., callback=dump_message)

    >>> while True:
    ...    connection.drain_events()

Poll
----

    >>> message = channel.basic_get(queue, ...)
    >>> if message:
    ...     dump_message(message)
    ...     print("Body:'%s' Properties:'%s' DeliveryInfo:'%s'" % (
    ...         message.body, message.properties, message.delivery_info))


Other
-----

    >>> channel.queue_unbind(queue, ...)
    >>> channel.close()
    >>> connection.close()

