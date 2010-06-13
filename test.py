import unittest2 as unittest

from pylibrabbitmq import Message, Connection


class TestChannel(unittest.TestCase):

    def test_send_message(self):
        connection = Connection()
        connection.connect()
        channel = connection.channel()
        message = Message("the quick brown fox jumps over the lazy dog",
                content_type="application/json",
                content_encoding="utf-8")
        channel.basic_publish(message, "celery", "celery")
        channel.close()
        connection.close()
