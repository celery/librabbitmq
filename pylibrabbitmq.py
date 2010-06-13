import _pyrabbitmq

__all__ = ["Connection"]
__version__ = "0.0.1"

class Channel(object):

    def __init__(self, conn, chanid):
        self.conn = conn
        self.chanid = chanid

    def basic_publish(self, message, exchange=None, routing_key=None,
            mandatory=False, immediate=False):
        self.conn._basic_publish(exchange, routing_key, message,
                self.chanid, mandatory, immediate)

    def close(self):
        self.conn._remove_channel(self)



class Connection(_pyrabbitmq.connection):
    curchan = 0
    channels = set()

    def __init__(self, hostname="localhost", port=5672, userid="guest",
            password="guest", vhost="/"):
        super(Connection, self).__init__(hostname=hostname, port=port,
                                     userid=userid, password=password,
                                     vhost=vhost)

    def channel(self):
        self.curchan += 1
        self._channel_open(self.curchan)
        channel = Channel(self, self.curchan)
        self.channels.add(channel)
        return channel

    def _remove_channel(self, channel):
        self._channel_close(channel.chanid)
        self.channels.remove(channel)

