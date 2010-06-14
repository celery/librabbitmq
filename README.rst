==================================================
 pylibrabbitmq - Python bindings to librabbitmq-c
==================================================

.. contents::
    :local:

Experimental Python bindings to the RabbitMQ C-library `librabbitmq`_.


You should probably use `amqplib`_ instead, but when needed you can 
come back to this if the extra performance is needed.

.. _`librabbitmq`: http://hg.rabbitmq.com/rabbitmq-c/
.. _`amqplib`: http://barryp.org/software/py-amqplib/

Installation
============

To install you need to compile `librabbitmq`::

    $ mkdir -p /opt/Build/rabbit
    $ cd /opt/Build/rabbit
    $ hg clone http://hg.rabbitmq.com/rabbitmq-codegen/
    $ hg clone http://hg.rabbitmq.com/rabbitmq-c/
    $ cd rabbitmq-c
    $ autoreconf -i
    $ ./configure
    $ make
    $ make install

Then you can install this package::

    $ cd pylibrabbitmq-x.x.x
    $ python setup.py install

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

License
=======

This software is licensed under the ``Mozilla Public License``.
See the ``LICENSE-MPL-RabbitMQ`` file in the top distribution directory
for the full license text.

.. # vim: syntax=rst expandtab tabstop=4 shiftwidth=4 shiftround
