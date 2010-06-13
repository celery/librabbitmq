import unittest2 as unittest

from pylibrabbitmq import Message, Connection


class TestChannel(unittest.TestCase):

    def setUp(self):
        self.connection = Connection()
        self.connection.connect()
        self.channel = self.connection.channel()

    def test_send_message(self):
        message = Message("the quick brown fox jumps over the lazy dog",
                content_type="application/json",
                content_encoding="utf-8")
        self.channel.basic_publish(message, "celery", "celery")

    def test_exchange_declare(self):
        self.channel.exchange_declare("tesxxx", "direct")

    def test_queue_declare(self):
        self.channel.queue_declare("tesxxx")
        self.channel.queue_bind("tesxxx", "tesxxx", "rkey")

    def tearDown(self):
        self.channel.close()
        self.connection.close()
