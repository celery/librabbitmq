from itertools import count

import _pyrabbitmq

__version__ = _pyrabbitmq.__version__
__author__ = _pyrabbitmq.__author__
__contact__ = _pyrabbitmq.__contact__
__homepage__ = _pyrabbitmq.__homepage__
__docformat__ = "restructuredtext"

ConnectionError = _pyrabbitmq.ConnectionError
ChannelError = _pyrabbitmq.ChannelError


__version__ = "0.2.0"
__all__ = ["Connection", "Message", "ConnectionError", "ChannelError"]


class Message(object):

    def __init__(self, body, properties=None, delivery_info=None,
            channel=None, **kwargs):
        if properties is None:
            properties = {}
        if delivery_info is None:
            delivery_info = {}
        self.body = body
        self.properties = properties
        self.delivery_info = delivery_info
        self.channel = channel

    def ack(self):
        return self.channel.basic_ack(self.delivery_info["delivery_tag"])

    def reject(self):
        return self.channel.basic_reject(self.delivery_info["delivery_tag"])


class Channel(object):
    is_open = False

    def __init__(self, connection, channel_id):
        self.connection = connection
        self.channel_id = channel_id
        self.next_consumer_tag = count(1).next
        self._callbacks = {}

    def basic_qos(self, prefetch_size=0, prefetch_count=0, _global=False):
        return self.connection._basic_qos(prefetch_size, prefetch_count, _global,
                self.channel_id)

    def flow(self, enabled):
        pass

    def basic_get(self, queue="", noack=False):
        frame = self.connection._basic_get(queue, noack, self.channel_id)
        if frame is not None:
            return(Message(frame["body"],
                           frame["properties"],
                           frame["delivery_info"],
                           self))

    def basic_consume(self, queue="", consumer_tag=None, no_local=False,
            no_ack=False, exclusive=False, callback=None, nowait=False):
        if consumer_tag is None:
            consumer_tag = str(self.next_consumer_tag())
        self.connection._basic_consume(queue, consumer_tag, no_local,
                no_ack, exclusive, self.channel_id)
        self._callbacks[consumer_tag] = callback

    def _event(self, event):
        assert event["channel"] == self.channel_id
        tag = event["delivery_info"]["consumer_tag"]
        if tag in self._callbacks:
            message = Message(event["body"],
                              event["properties"],
                              event["delivery_info"],
                              channel=self)
            self._callbacks[tag](message)

    def basic_ack(self, delivery_tag, multiple=False):
        return self.connection._basic_ack(delivery_tag, multiple,
                                          self.channel_id)

    def basic_reject(self, delivery_tag, requeue=True):
        return self.connection._basic_reject(delivery_tag, requeue,
                                          self.channel_id)

    def basic_cancel(self, *args, **kwargs):
        pass

    def basic_publish(self, message, exchange="", routing_key="",
            mandatory=False, immediate=False):
        return self.connection._basic_publish(self.channel_id,
                message.body, exchange, routing_key, message.properties,
                mandatory, immediate)

    def queue_purge(self, queue, nowait=False):
        return self.connection._queue_purge(queue, nowait, self.channel_id)

    def exchange_declare(self, exchange="", type="direct",
            passive=False, durable=False, auto_delete=False, arguments=None,
            nowait=False):
        return self.connection._exchange_declare(exchange, type,
                self.channel_id, passive, durable, auto_delete)

    def queue_declare(self, queue="", passive=False, durable=False,
            exclusive=False, auto_delete=False, arguments=None,
            nowait=False):
        return self.connection._queue_declare(queue,
                self.channel_id, passive, durable, exclusive, auto_delete)

    def queue_bind(self, queue="", exchange="", routing_key="",
            arguments=None, nowait=False):
        return self.connection._queue_bind(queue, exchange, routing_key,
                self.channel_id)

    def queue_unbind(self, queue="", exchange="", binding_key="",
            nowait=False):
        return self.connection._queue_unbind(queue, exchange, binding_key,
                self.channel_id)

    def close(self):
        self.connection._remove_channel(self)


class Connection(_pyrabbitmq.connection):
    """Create a connection to the specified host, which should be
    a ``'host[:port]'`` string, such as ``'localhost'``, or ``'1.2.3.4:5672'``

    Host defaults to ``'localhost'``, if a port is not specified
    then 5672 is used.

    """
    Channel = Channel

    channels = {}
    channel_max = 0xffff
    frame_max = 131072
    heartbeat = 0

    def __init__(self, host="localhost", userid="guest", password="guest",
            virtual_host="/", port=5672, **kwargs):

        if ":" in host:
            host, port = host.split(":")

        self.hostname = host
        self.port = int(port)
        self.userid = userid
        self.password = password
        self.virtual_host = virtual_host
        super(Connection, self).__init__(hostname=host, port=self.port,
                                     userid=userid, password=password,
                                     virtual_host=virtual_host,
                                     channel_max=self.channel_max,
                                     frame_max=self.frame_max,
                                     heartbeat=self.heartbeat)
        self._do_connect()

    def drain_events(self, timeout=None):
        event = self._basic_recv()
        if event:
            self.channels[event["channel"]]._event(event)

    def channel(self, channel_id=None):
        if channel_id is None:
            channel_id = self._get_free_channel_id()
        if channel_id in self.channels:
            return self.channels[channel_id]

        self._channel_open(channel_id)
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

    def _get_free_channel_id(self):
        for i in xrange(1, self.channel_max+1):
            if i not in self.channels:
                return i
        raise ConnectionError(
                "No free channel ids, current=%d, channel_max=%d" % (
                    len(self.channels), self.channel_max))

