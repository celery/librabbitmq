# coding: utf-8
from __future__ import absolute_import

from six.moves import xrange

import socket
import unittest
from array import array
import time

from librabbitmq import Message, Connection, ConnectionError, ChannelError
TEST_QUEUE = 'pyrabbit.testq'


class test_Connection(unittest.TestCase):
    def test_connection_defaults(self):
        """Test making a connection with the default settings."""
        with Connection() as connection:
            self.assertGreaterEqual(connection.fileno(), 0)

    def test_connection_invalid_host(self):
        """Test connection to an invalid host fails."""
        # Will fail quickly as OS will reject it.
        with self.assertRaises(ConnectionError):
            Connection(host="255.255.255.255")

    def test_connection_invalid_port(self):
        """Test connection to an invalid port fails."""
        # Will fail quickly as OS will reject it.
        with self.assertRaises(ConnectionError):
            Connection(port=0)

    def test_connection_timeout(self):
        """Test connection timeout."""
        # Can't rely on local OS being configured to ignore SYN packets
        # (OS would normally reply with RST to closed port). To test the
        # timeout, need to connect to something that is either slow, or
        # never responds.
        start_time = time.time()
        with self.assertRaises(ConnectionError):
            Connection(host="google.com", port=81, connect_timeout=3)
        took_time = time.time() - start_time
        # Allow some leaway to avoid spurious test failures.
        self.assertGreaterEqual(took_time, 2)
        self.assertLessEqual(took_time, 4)

    def test_get_free_channel_id(self):
        with Connection() as connection:
            assert connection._get_free_channel_id() == 1
            assert connection._get_free_channel_id() == 2

    def test_get_free_channel_id__channels_full(self):
        with Connection() as connection:
            for _ in range(connection.channel_max):
                connection._get_free_channel_id()
            with self.assertRaises(ConnectionError):
                connection._get_free_channel_id()

    def test_channel(self):
        with Connection() as connection:
            self.assertEqual(connection._used_channel_ids, array('H'))
            connection.channel()
            self.assertEqual(connection._used_channel_ids, array('H', (1,)))


class test_Channel(unittest.TestCase):

    def setUp(self):
        self.connection = Connection(host='localhost:5672', userid='guest',
                                     password='guest', virtual_host='/')
        self.channel = self.connection.channel()
        self.channel.queue_delete(TEST_QUEUE)
        self._queue_declare()

    def test_send_message(self):
        message = Message(
            channel=self.channel,
            body='the quick brown fox jumps over the lazy dog',
            properties=dict(content_type='application/json',
                            content_encoding='utf-8'))
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)
        self.assertGreater(self.channel.queue_purge(TEST_QUEUE), 2)
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)

    def test_nonascii_headers(self):
        message = Message(
            channel=self.channel,
            body='the quick brown fox jumps over the lazy dog',
            properties=dict(content_type='application/json',
                            content_encoding='utf-8',
                            headers={'key': r'¯\_(ツ)_/¯'}))
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)

    def _queue_declare(self):
        self.channel.exchange_declare(TEST_QUEUE, 'direct')
        x = self.channel.queue_declare(TEST_QUEUE)
        self.assertEqual(x.message_count, x[1])
        self.assertEqual(x.consumer_count, x[2])
        self.assertEqual(x.queue, TEST_QUEUE)
        self.channel.queue_bind(TEST_QUEUE, TEST_QUEUE, TEST_QUEUE)

    def test_basic_get_ack(self):
        message = Message(
            channel=self.channel,
            body='the quick brown fox jumps over the lazy dog',
            properties=dict(content_type='application/json',
                            content_encoding='utf-8'))
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)
        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)
        while True:
            x = self.channel.basic_get(TEST_QUEUE)
            if x:
                break
        self.assertIs(self.channel, x.channel)
        self.assertIn('message_count', x.delivery_info)
        self.assertIn('redelivered', x.delivery_info)
        self.assertEqual(x.delivery_info['routing_key'], TEST_QUEUE)
        self.assertEqual(x.delivery_info['exchange'], TEST_QUEUE)
        self.assertTrue(x.delivery_info['delivery_tag'])
        self.assertTrue(x.properties['content_type'])
        self.assertTrue(x.body)
        x.ack()

    def test_timeout_burst(self):
        """Check that if we have a large burst of messages in our queue
        that we can fetch them with a timeout without needing to receive
        any more messages."""

        message = Message(
            channel=self.channel,
            body='the quick brown fox jumps over the lazy dog',
            properties=dict(content_type='application/json',
                            content_encoding='utf-8'))

        for i in xrange(100):
            self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)

        messages = []

        def cb(x):
            messages.append(x)
            x.ack()

        self.channel.basic_consume(TEST_QUEUE, callback=cb)
        for i in xrange(100):
            self.connection.drain_events(timeout=0.2)

        self.assertEqual(len(messages), 100)

    def test_timeout(self):
        """Check that our ``drain_events`` call actually times out if
        there are no messages."""
        message = Message(
            channel=self.channel,
            body='the quick brown fox jumps over the lazy dog',
            properties=dict(content_type='application/json',
                            content_encoding='utf-8'))

        self.channel.basic_publish(message, TEST_QUEUE, TEST_QUEUE)

        messages = []

        def cb(x):
            messages.append(x)
            x.ack()

        self.channel.basic_consume(TEST_QUEUE, callback=cb)
        self.connection.drain_events(timeout=0.1)

        with self.assertRaises(socket.timeout):
            self.connection.drain_events(timeout=0.1)
        self.assertEqual(len(messages), 1)

    def test_close(self):
        self.assertEqual(self.connection._used_channel_ids, array('H', (1,)))
        self.channel.close()
        self.channel = None
        self.assertEqual(self.connection._used_channel_ids, array('H'))

    def tearDown(self):
        if self.channel and self.connection.connected:
            self.channel.queue_purge(TEST_QUEUE)
            self.channel.close()
        if self.connection:
            try:
                self.connection.close()
            except ConnectionError:
                pass


class test_Delete(unittest.TestCase):

    def setUp(self):
        self.connection = Connection(host='localhost:5672', userid='guest',
                                     password='guest', virtual_host='/')
        self.channel = self.connection.channel()
        self.TEST_QUEUE = 'pyrabbitmq.testq2'
        self.channel.queue_delete(self.TEST_QUEUE)

    def test_delete(self):
        """Test that we can declare a channel delete it, and then declare with
        different properties"""

        self.channel.exchange_declare(self.TEST_QUEUE, 'direct')
        self.channel.queue_declare(self.TEST_QUEUE)
        self.channel.queue_bind(
            self.TEST_QUEUE, self.TEST_QUEUE, self.TEST_QUEUE,
        )

        # Delete the queue
        self.channel.queue_delete(self.TEST_QUEUE)

        # Declare it again
        x = self.channel.queue_declare(self.TEST_QUEUE, durable=True)
        self.assertEqual(x.queue, self.TEST_QUEUE)

        self.channel.queue_delete(self.TEST_QUEUE)

    def test_delete_empty(self):
        """Test that the queue doesn't get deleted if it is not empty"""
        self.channel.exchange_declare(self.TEST_QUEUE, 'direct')
        self.channel.queue_declare(self.TEST_QUEUE)
        self.channel.queue_bind(self.TEST_QUEUE, self.TEST_QUEUE,
                                self.TEST_QUEUE)

        message = Message(
            channel=self.channel,
            body='the quick brown fox jumps over the lazy dog',
            properties=dict(content_type='application/json',
                            content_encoding='utf-8'))

        self.channel.basic_publish(message, self.TEST_QUEUE, self.TEST_QUEUE)

        with self.assertRaises(ChannelError):
            self.channel.queue_delete(self.TEST_QUEUE, if_empty=True)

        # We need to make a new channel after a ChannelError
        self.channel = self.connection.channel()

        x = self.channel.basic_get(self.TEST_QUEUE)
        self.assertTrue(x.body)

        self.channel.queue_delete(self.TEST_QUEUE, if_empty=True)

    def tearDown(self):
        if self.channel and self.connection.connected:
            self.channel.queue_purge(TEST_QUEUE)
            self.channel.close()
        if self.connection:
            try:
                self.connection.close()
            except ConnectionError:
                pass
