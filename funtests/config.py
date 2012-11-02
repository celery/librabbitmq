import os

BROKER_HOST = os.environ.get('BROKER_HOST', 'localhost')
BROKER_PORT = int(os.environ.get('BROKER_PORT', 5672))
BROKER_VHOST = os.environ.get('BROKER_VHOST', '/')
BROKER_USER = os.environ.get('BROKER_USER', 'guest')
BROKER_PASSWORD = os.environ.get('BROKER_PASSWORD', 'guest')

from functools import partial
from unittest2 import TestCase
from uuid import uuid4


class BrokerCase(TestCase):

    def setUp(self):
        import librabbitmq
        self.cleanup_queues = set()
        self.Connection = partial(librabbitmq.Connection,
            host=BROKER_HOST,
            port=BROKER_PORT,
            userid=BROKER_USER,
            password=BROKER_PASSWORD,
            virtual_host=BROKER_VHOST,
        )
        self.mod = librabbitmq
        self.ConnectionError = self.mod.ConnectionError
        self.ChannelError = self.mod.ChannelError

    def tearDown(self):
        try:
            for name in self.cleanup_queues:
                with self.Connection() as conn:
                    with conn.channel() as chan:
                        try:
                            chan.queue_delete(name)
                        except (self.ConnectionError, self.ChannelError):
                            pass
        finally:
            self.cleanup_queues.clear()

    def uses_queue(self, name, register=True):
        register and self.cleanup_queues.add(name)
        return name

    def new_queue(self, register=True):
        return self.uses_queue('lrmqFUNTEST.%s' % (uuid4(), ), register)
