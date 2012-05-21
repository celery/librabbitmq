================================================================
 librabbitmq - Python AMQP Client using the rabbitmq-c library.
================================================================

:Version: 0.9.1
:Download: http://pypi.python.org/pypi/librabbitmq/
:Code: http://github.com/celery/librabbitmq/
:Keywords: rabbitmq, amqp, messaging, librabbitmq, rabbitmq-c, python,
           kombu, celery

.. contents::
    :local:

Python bindings to the RabbitMQ C-library `rabbitmq-c`_.
Supported by Kombu and Celery.

.. _`rabbitmq-c`: https://github.com/alanxz/rabbitmq-c

Installation
============

Install via pip::

    $ pip install librabbitmq

or, install via easy_install::

    $ easy_install librabbitmq

Downloading and installing from source
--------------------------------------

Download the latest version from
    http://pypi.python.org/pypi/librabbitmq/

Then install it by doing the following,::

    $ tar xvfz librabbitmq-0.0.0.tar.gz
    $ cd librabbitmq-0.0.0
    $ python setup.py build
    # python setup.py install # as root

Using the development version
-----------------------------

You can clone the repository by doing the following::

    $ git clone git://github.com/celery/librabbitmq.git

Then install it by doing the following::

    $ cd librabbitmq
    $ make install        # or make develop

Examples
========

Using with Kombu::

    >>> from kombu import Connection
    >>> x = Connection("librabbitmq://")


Stand-alone::

    >>> from librabbitmq import Connection, Message

    >>> conn = Connection(host="localhost", userid="guest",
    ...                   password="guest", virtual_host="/")

    >>> channel = conn.channel()
    >>> channel.exchange_declare(exchange, type, ...)
    >>> channel.queue_declare(queue, ...)
    >>> channel.queue_bind(queue, exchange, routing_key)

Producing
---------

::

    >>> m = Message(body, content_type=None, content_encoding=None,
    ...             delivery_mode=1)
    >>> channel.basic_publish(m, exchange, routing_key, ...)

Consuming
---------

::

    >>> def dump_message(message):
    ...     print("Body:'%s', Proeprties:'%s', DeliveryInfo:'%s'" % (
    ...         message.body, message.properties, message.delivery_info))
    ...     message.ack()

    >>> channel.basic_consume(queue, ..., callback=dump_message)

    >>> while True:
    ...    connection.drain_events()

Poll
----

::

    >>> message = channel.basic_get(queue, ...)
    >>> if message:
    ...     dump_message(message)
    ...     print("Body:'%s' Properties:'%s' DeliveryInfo:'%s'" % (
    ...         message.body, message.properties, message.delivery_info))


Other
-----

::

    >>> channel.queue_unbind(queue, ...)
    >>> channel.close()
    >>> connection.close()

License
=======

This software is licensed under the ``Mozilla Public License``.
See the ``LICENSE-MPL-RabbitMQ`` file in the top distribution directory
for the full license text.

.. # vim: syntax=rst expandtab tabstop=4 shiftwidth=4 shiftround
