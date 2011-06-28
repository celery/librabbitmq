import socket
import unittest2 as unittest

from pylibrabbitmq import Message, Connection, ConnectionError
TEST_QUEUE = "pyrabbit.testq"


class test_Channel(unittest.TestCase):

    def setUp(self):
        self.connection = Connection(host="localhost:5672", userid="guest",
                                     password="guest", virtual_host="/")
        self.channel = self.connection.channel()
        self._queue_declare()

    def test_send_message(self):
        message = Message("the quick brown fox jumps over the lazy dog",
                properties=dict(content_type="application/json",
                                content_encoding="utf-8"))
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE);
        self.assertGreater(self.channel.queue_purge(TEST_QUEUE), 2)
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE);

    def _queue_declare(self):
        self.channel.exchange_declare(TEST_QUEUE, "direct")
        x = self.channel.queue_declare(TEST_QUEUE)
        self.assertIn("message_count", x)
        self.assertIn("consumer_count", x)
        self.assertEqual(x["queue"], TEST_QUEUE)
        self.channel.queue_bind(TEST_QUEUE, TEST_QUEUE, TEST_QUEUE)

    def test_basic_get_ack(self):
        message = Message("the quick brown fox jumps over the lazy dog",
                properties=dict(content_type="application/json",
                                content_encoding="utf-8"))
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)
        while True:
            x = self.channel.basic_get(TEST_QUEUE)
            if x:
                break;
        self.assertIs(self.channel, x.channel)
        self.assertIn("message_count", x.delivery_info)
        self.assertIn("redelivered", x.delivery_info)
        self.assertEqual(x.delivery_info["routing_key"], TEST_QUEUE)
        self.assertEqual(x.delivery_info["exchange"], TEST_QUEUE)
        self.assertTrue(x.delivery_info["delivery_tag"])
        self.assertTrue(x.properties["content_type"])
        self.assertTrue(x.body)
        x.ack()

    def tearDown(self):
        if self.channel:
            self.channel.queue_purge(TEST_QUEUE)
            self.channel.close()
        if self.connection:
            try:
                self.connection.close()
            except ConnectionError:
                pass
