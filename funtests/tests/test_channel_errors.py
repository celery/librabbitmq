from __future__ import absolute_import

from config import BrokerCase

from librabbitmq import ChannelError


class test_channel_failures(BrokerCase):

    def test_channel_replaced_on_exception(self):
        with self.Connection() as conn:
            chan = conn.channel()
            self.assertTrue(chan)

            with self.assertRaises(ChannelError):
                chan.queue_declare(self.new_queue(False), passive=True)
            self.assertTrue(chan.queue_declare(self.new_queue(False)))
