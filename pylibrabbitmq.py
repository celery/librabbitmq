from itertools import count

import _pyrabbitmq

__version__ = _pyrabbitmq.__version__
__author__ = _pyrabbitmq_.__author__
__contact__ = _pyrabbitmq.__contact__
__homepage__ = _pyrabbitmq.__homepage__
__docformat__ = "restructuredtext"

ConnectionError = _pyrabbitmq.ConnectionError
ChannelError = _pyrabbitmq.ChannelError


__version__ = "0.0.1"
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


class Channel(object):
    is_open = False

    def __init__(self, conn, channel_id):
        self.conn = conn
        self.channel_id = channel_id
        self.next_consumer_tag = count(1).next
        self._callbacks = {}

    def basic_get(self, queue="", noack=False):
        frame = self.conn._basic_get(queue, noack, self.channel_id)
        if frame is not None:
            return(Message(frame["body"],
                           frame["properties"],
                           frame["delivery_info"],
                           self))

    def basic_consume(self, queue="", consumer_tag=None, no_local=False,
            no_ack=False, exclusive=False, callback=None):
        if consumer_tag is None:
            consumer_tag = str(self.next_consumer_tag())
        self.conn._basic_consume(queue, consumer_tag, no_local,
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
        return self.conn._basic_ack(delivery_tag, multiple, self.channel_id)

    def basic_cancel(self, *args, **kwargs):
        pass

    def basic_publish(self, message, exchange="", routing_key="",
            mandatory=False, immediate=False):
        return self.conn._basic_publish(exchange=exchange,
                routing_key=routing_key,
                message=message.body,
                properties=message.properties,
                channel=self.channel_id,
                mandatory=mandatory,
                immediate=immediate)

    def queue_purge(self, queue, no_wait=False):
        return self.conn._queue_purge(queue, no_wait, self.channel_id)

    def exchange_declare(self, exchange="", type="direct",
            passive=False, durable=False, auto_delete=False, arguments=None):
        return self.conn._exchange_declare(exchange, type,
                self.channel_id, passive, durable, auto_delete)

    def queue_declare(self, queue="", passive=False, durable=False,
            exclusive=False, auto_delete=False, arguments=None):
        return self.conn._queue_declare(queue,
                self.channel_id, passive, durable, exclusive, auto_delete)

    def queue_bind(self, queue="", exchange="", routing_key="", arguments=None):
        return self.conn._queue_bind(queue, exchange, routing_key,
                self.channel_id)

    def queue_unbind(self, queue="", exchange="", binding_key=""):
        return self.conn._queue_unbind(queue, exchange, binding_key,
                self.channel_id)

    def close(self):
        self.conn._remove_channel(self)



class Connection(_pyrabbitmq.connection):
    channels = {}
    channel_max = 131072

    def __init__(self, hostname="localhost", port=5672, userid="guest",
            password="guest", vhost="/"):
        self.hostname = hostname
        self.port = port
        self.userid = userid
        self.password = password
        self.vhost = vhost
        super(Connection, self).__init__(hostname=hostname, port=port,
                                     userid=userid, password=password,
                                     vhost=vhost)

    def drain_events(self):
        event = self._basic_recv()
        if event is not None:
            self.channels[event["channel"]]._event(event)

    def wait_multi(self, *args, **kwargs):
        # for celery amqp backend
        self.drain_events()

    def channel(self, channel_id=None):
        if channel_id is None:
            channel_id = self._get_free_channel_id()
        if channel_id in self.channels:
            return self.channels[channel_id]

        self._channel_open(channel_id)
        channel = Channel(self, channel_id)
        channel.is_open = True
        self.channels[channel_id] = channel
        return channel

    def _remove_channel(self, channel):
        channel.is_open = False
        self._channel_close(channel.channel_id)
        self.channels.pop(channel.channel_id, None)

    def _get_free_channel_id(self):
        for i in xrange(1, self.channel_max+1):
            if i not in self.channels:
                return i
        raise ConnectionError(
                "No free channel ids, current=%d, channel_max=%d" % (
                    len(self.channels), self.channel_max))

