import itertools
import socket

import _librabbitmq

from amqp.protocol import queue_declare_ok_t
from array import array

__version__ = _librabbitmq.__version__
__author__ = _librabbitmq.__author__
__contact__ = _librabbitmq.__contact__
__homepage__ = _librabbitmq.__homepage__
__docformat__ = 'restructuredtext'

ConnectionError = _librabbitmq.ConnectionError
ChannelError = _librabbitmq.ChannelError


__version__ = '1.5.2'
__all__ = ['Connection', 'Message', 'ConnectionError', 'ChannelError']


class Message(object):

    def __init__(self, channel, properties, delivery_info, body):
        self.channel = channel
        self.properties = properties
        self.delivery_info = delivery_info
        self.body = body

    def ack(self):
        return self.channel.basic_ack(self.delivery_info['delivery_tag'])

    def reject(self):
        return self.channel.basic_reject(self.delivery_info['delivery_tag'])


class Channel(object):
    Message = Message
    is_open = False

    def __init__(self, connection, channel_id):
        self.connection = connection
        self.channel_id = channel_id
        self.next_consumer_tag = itertools.count(1).next
        self.no_ack_consumers = set()

    def __enter__(self):
        return self

    def __exit__(self, *exc_info):
        self.close()

    def basic_qos(self, prefetch_size=0, prefetch_count=0, _global=False):
        return self.connection._basic_qos(self.channel_id,
                prefetch_size, prefetch_count, _global)

    def flow(self, active):
        return self.connection._flow(self.channel_id, active)

    def basic_recover(self, requeue=True):
        return self.connection._basic_recover(self.channel_id, requeue)

    def basic_get(self, queue='', no_ack=False):
        frame = self.connection._basic_get(self.channel_id, queue, no_ack)
        if frame is not None:
            return(self.Message(self,
                                frame['properties'],
                                frame['delivery_info'],
                                frame['body']))

    def basic_consume(self, queue='', consumer_tag=None, no_local=False,
            no_ack=False, exclusive=False, callback=None, arguments=None,
            nowait=False):
        if consumer_tag is None:
            consumer_tag = self.next_consumer_tag()
        consumer_tag = self.connection._basic_consume(self.channel_id,
                queue, str(consumer_tag), no_local, no_ack, exclusive,
                arguments or {})
        self.connection.callbacks[self.channel_id][consumer_tag] = callback
        if no_ack:
            self.no_ack_consumers.add(consumer_tag)
        return consumer_tag

    def basic_ack(self, delivery_tag, multiple=False):
        return self.connection._basic_ack(self.channel_id,
                    delivery_tag, multiple)

    def basic_reject(self, delivery_tag, requeue=True):
        return self.connection._basic_reject(self.channel_id,
                    delivery_tag, requeue)

    def basic_cancel(self, consumer_tag, **kwargs):
        self.no_ack_consumers.discard(consumer_tag)
        if self.connection:
            try:
                callbacks = self.connection.callbacks[self.channel_id]
            except KeyError:
                pass
            else:
                callbacks.pop(consumer_tag, None)
            self.connection._basic_cancel(self.channel_id, consumer_tag)

    def basic_publish(self, body, exchange='', routing_key='',
            mandatory=False, immediate=False, **properties):
        if isinstance(body, tuple):
            body, properties = body
        elif isinstance(body, self.Message):
            body, properties = body.body, body.properties
        return self.connection._basic_publish(self.channel_id,
                body, exchange, routing_key, properties,
                mandatory or False, immediate or False)

    def queue_purge(self, queue, nowait=False):
        return self.connection._queue_purge(self.channel_id, queue, nowait)

    def exchange_declare(self, exchange='', type='direct',
            passive=False, durable=False, auto_delete=False, arguments=None,
            nowait=False):
        """Declare exchange.

        :keyword auto_delete: Not recommended and so it is ignored.

        """
        return self.connection._exchange_declare(self.channel_id,
                exchange, type, passive, durable, auto_delete, arguments or {})

    def exchange_delete(self, exchange='', if_unused=False, nowait=False):
        return self.connection._exchange_delete(
                self.channel_id, exchange, if_unused)

    def queue_declare(self, queue='', passive=False, durable=False,
                      exclusive=False, auto_delete=False, arguments=None,
                      nowait=False):
        return queue_declare_ok_t(
            *self.connection._queue_declare(
                self.channel_id, queue, passive, durable,
                exclusive, auto_delete, arguments or {},
            )
        )

    def queue_bind(self, queue='', exchange='', routing_key='',
            arguments=None, nowait=False):
        return self.connection._queue_bind(self.channel_id,
                queue, exchange, routing_key, arguments or {})

    def queue_unbind(self, queue='', exchange='', routing_key='',
            arguments=None, nowait=False):
        return self.connection._queue_unbind(self.channel_id,
                queue, exchange, routing_key, arguments or {})

    def queue_delete(self, queue='', if_unused=False, if_empty=False,
            nowait=False):
        """nowait argument is not supported."""
        return self.connection._queue_delete(self.channel_id,
                queue, if_unused, if_empty)

    def close(self):
        if self.connection:
            self.connection._remove_channel(self)


class Connection(_librabbitmq.Connection):
    """Create a connection to the specified host, which should be
    a ``'host[:port]'`` string, such as ``'localhost'``, or ``'1.2.3.4:5672'``

    Host defaults to ``'localhost'``, if a port is not specified
    then 5672 is used.

    """
    Message = Message
    Channel = Channel

    def __init__(self, host='localhost', userid='guest', password='guest',
            virtual_host='/', port=5672, channel_max=0xffff,
            frame_max=131072, heartbeat=0, lazy=False, **kwargs):
        if ':' in host:
            host, port = host.split(':')
        super(Connection, self).__init__(hostname=host, port=int(port),
                                         userid=userid, password=password,
                                         virtual_host=virtual_host,
                                         channel_max=channel_max,
                                         frame_max=frame_max,
                                         heartbeat=heartbeat)
        self.channels = {}
        self._avail_channel_ids = array('H', xrange(self.channel_max, 0, -1))
        if not lazy:
            self.connect()

    def __enter__(self):
        return self

    def __exit__(self, *exc_info):
        self.close()

    def reconnect(self):
        self.close()
        self.connect()

    def drain_events(self, timeout=None):
        # we rewrite to socket.timeout here, as this is what kombu-patched
        # amqplib uses.
        if timeout == 0.0:
            timeout = -1
        elif timeout is None:
            timeout = 0.0
        else:
            timeout = float(timeout)
        self._basic_recv(timeout)

    def channel(self, channel_id=None):
        if channel_id is None:
            channel_id = self._get_free_channel_id()
        elif channel_id in self.channels:
            return self.channels[channel_id]

        self._channel_open(channel_id)
        self.callbacks[channel_id] = {}
        channel = self.Channel(self, channel_id)
        channel.is_open = True
        self.channels[channel_id] = channel
        return channel

    def _remove_channel(self, channel):
        channel.is_open = False
        try:
            self._channel_close(channel.channel_id)
        except ChannelError:
            pass
        self.channels.pop(channel.channel_id, None)
        self.callbacks.pop(channel.channel_id, None)
        self._avail_channel_ids.append(channel.channel_id)

    def _get_free_channel_id(self):
        try:
            return self._avail_channel_ids.pop()
        except IndexError:
            raise ConnectionError(
                'No free channel ids, current=%d, channel_max=%d' % (
                    len(self.channels), self.channel_max))

    def close(self):
        try:
            self._close()
        except ConnectionError:
            pass
