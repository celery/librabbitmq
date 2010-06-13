==================================================
 pylibrabbitmq - Python bindings to librabbitmq-c
==================================================

**EXPERIMENTAL!**

Only supports sending and creating exchanges/queues.
Does not support headers or ``arguments`` yet.


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

    >>> m = Message(body, content_type=None, content_encoding=None,
    ...             delivery_mode=1)
    >>> channel.basic_publish(m, exchange, routing_key, ...)


    >>> channel.close()
    >>> connection.close()

