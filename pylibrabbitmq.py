from itertools import count

import _pyrabbitmq

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

    def __init__(self, conn, chanid):
        self.conn = conn
        self.chanid = chanid
        self.next_consumer_tag = count(1).next
        self._callbacks = {}

    def basic_get(self, queue="", noack=False):
        d = self.conn._basic_get(queue, noack, self.chanid)
        if d is not None:
            return Message(channel=self, **d)

    def basic_consume(self, queue="", consumer_tag=None, no_local=False,
            no_ack=False, exclusive=False, callback=None):
        if consumer_tag is None:
            consumer_tag = str(self.next_consumer_tag())
        self.conn._basic_consume(queue, consumer_tag, no_local,
                no_ack, exclusive, self.chanid)
        self._callbacks[consumer_tag] = callback

    def _event(self, event):
        assert event["channel"] == self.chanid
        tag = event["delivery_info"]["consumer_tag"]
        if tag in self._callbacks:
            message = Message(event["body"],
                              event["properties"],
                              event["delivery_info"],
                              channel=self)
            self._callbacks[tag](message)

    def basic_ack(self, delivery_tag, multiple=False):
        return self.conn._basic_ack(delivery_tag, multiple, self.chanid)

    def basic_publish(self, message, exchange="", routing_key="",
            mandatory=False, immediate=False):
        return self.conn._basic_publish(exchange=exchange,
                routing_key=routing_key,
                message=message.body,
                properties=message.properties,
                channel=self.chanid,
                mandatory=mandatory,
                immediate=immediate)

    def queue_purge(self, queue, no_wait=False):
        return self.conn._queue_purge(queue, no_wait, self.chanid)

    def exchange_declare(self, exchange="", exchange_type="direct",
            passive=False, durable=False, auto_delete=False):
        return self.conn._exchange_declare(exchange, exchange_type,
                self.chanid, passive, durable, auto_delete)

    def queue_declare(self, queue="", passive=False, durable=False,
            exclusive=False, auto_delete=False):
        return self.conn._queue_declare(queue,
                self.chanid, passive, durable, exclusive, auto_delete)

    def queue_bind(self, queue="", exchange="", routing_key=""):
        return self.conn._queue_bind(queue, exchange, routing_key, self.chanid)

    def queue_unbind(self, queue="", exchange="", binding_key=""):
        return self.conn._queue_unbind(queue, exchange, binding_key, self.chanid)

    def close(self):
        self.conn._remove_channel(self)



class Connection(_pyrabbitmq.connection):
    curchan = 0
    channels = {}

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

    def channel(self):
        # TODO need to reuse channel numbers.
        self.curchan += 1
        self._channel_open(self.curchan)
        channel = Channel(self, self.curchan)
        self.channels[channel.chanid] = channel
        return channel

    def _remove_channel(self, channel):
        self._channel_close(channel.chanid)
        self.channels.pop(channel.chanid, None)

