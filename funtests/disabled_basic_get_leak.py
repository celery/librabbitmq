import os

from librabbitmq import Connection

x = Connection()
c = x.channel()
c.exchange_declare('getmem')
c.queue_declare('getmem')
c.queue_bind('getmem', 'getmem', 'getmem')

from time import sleep

for i in xrange(10000):
    c.basic_publish('foo', exchange='getmem', routing_key='getmem')
    if not i % 1000:
        print('sent %s' % i)

for i in xrange(10000):
    assert c.basic_get('getmem', no_ack=True)
    if not i % 1000:
        print(i)
        os.system('sh -c "ps auxww | grep %d | grep -v grep "' % os.getpid())

