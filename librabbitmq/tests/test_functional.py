import socket
import unittest2 as unittest

from librabbitmq import Message, Connection, ConnectionError, ChannelError
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
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)
        self.assertGreater(self.channel.queue_purge(TEST_QUEUE), 2)
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)

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
                break
        self.assertIs(self.channel, x.channel)
        self.assertIn("message_count", x.delivery_info)
        self.assertIn("redelivered", x.delivery_info)
        self.assertEqual(x.delivery_info["routing_key"], TEST_QUEUE)
        self.assertEqual(x.delivery_info["exchange"], TEST_QUEUE)
        self.assertTrue(x.delivery_info["delivery_tag"])
        self.assertTrue(x.properties["content_type"])
        self.assertTrue(x.body)
        x.ack()

    def test_timeout_burst(self):
        """Check that if we have a large burst of messages in our queue
        that we can fetch them with a timeout without needing to receive
        any more messages."""

        message = Message("the quick brown fox jumps over the lazy dog",
                          properties=dict(content_type="application/json",
                                          content_encoding="utf-8"))

        for i in xrange(100):
            self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)

        messages = []

        def cb(x):
            messages.append(x)
            x.ack()

        self.channel.basic_consume(TEST_QUEUE, callback=cb)
        for i in xrange(100):
            self.connection.drain_events(timeout=0.2)

        self.assertEquals(len(messages), 100)

    def test_timeout(self):
        """Check that our ``drain_events`` call actually times out if
        there are no messages."""
        message = Message("the quick brown fox jumps over the lazy dog",
                          properties=dict(content_type="application/json",
                                          content_encoding="utf-8"))

        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)

        messages = []

        def cb(x):
            messages.append(x)
            x.ack()

        self.channel.basic_consume(TEST_QUEUE, callback=cb)
        self.connection.drain_events(timeout=0.1)

        self.assertRaises(socket.timeout,
                self.connection.drain_events, timeout=0.1)
        self.assertEquals(len(messages), 1)

    def tearDown(self):
        if self.channel:
            self.channel.queue_purge(TEST_QUEUE)
            self.channel.close()
        if self.connection:
            try:
                self.connection.close()
            except ConnectionError:
                pass

class test_Delete(unittest.TestCase):

    def setUp(self):
        self.connection = Connection(host="localhost:5672", userid="guest",
                                     password="guest", virtual_host="/")
        self.channel = self.connection.channel()
        self.TEST_QUEUE = "pyrabbitmq.testq2"

    def test_delete(self):
        """Test that we can declare a channel delete it, and then declare with
        different properties"""

        res = self.channel.exchange_declare(self.TEST_QUEUE, "direct")
        res =self.channel.queue_declare(self.TEST_QUEUE)
        res = self.channel.queue_bind(self.TEST_QUEUE, self.TEST_QUEUE,
                                self.TEST_QUEUE)

        # Delete the queue
        self.channel.queue_delete(self.TEST_QUEUE)

        # Declare it again
        x = self.channel.queue_declare(self.TEST_QUEUE, durable=True)
        self.assertIn("message_count", x)
        self.assertIn("consumer_count", x)
        self.assertEqual(x["queue"], self.TEST_QUEUE)

        self.channel.queue_delete(self.TEST_QUEUE)

    def test_delete_empty(self):
        """Test that the queue doesn't get deleted if it is not empty"""
        self.channel.exchange_declare(self.TEST_QUEUE, "direct")
        self.channel.queue_declare(self.TEST_QUEUE)
        self.channel.queue_bind(self.TEST_QUEUE, self.TEST_QUEUE,
                                self.TEST_QUEUE)

        message = Message("the quick brown fox jumps over the lazy dog",
                          properties=dict(content_type="application/json",
                                          content_encoding="utf-8"))

        self.channel.basic_publish(message, self.TEST_QUEUE, self.TEST_QUEUE)

        self.assertRaises(ChannelError, self.channel.queue_delete,
                          self.TEST_QUEUE, if_empty=True)
        #We need to make a new channel after a ChannelError
        self.channel = self.connection.channel()

        x = self.channel.basic_get(self.TEST_QUEUE)
        self.assertTrue(x.body)

        self.channel.queue_delete(self.TEST_QUEUE, if_empty=True)

    def tearDown(self):
        if self.channel:
            self.channel.queue_purge(TEST_QUEUE)
            self.channel.close()
        if self.connection:
            try:
                self.connection.close()
            except ConnectionError:
                pass
