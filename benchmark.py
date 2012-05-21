import timeit

QS = ("amqplib.benchmark", "librabbit.benchmark")


INIT_COMMON = """
connection = amqp.Connection(hostname="localhost", userid="guest",
password="guest", virtual_host="/")
channel = connection.channel()
channel.exchange_declare(Q, "direct")
channel.queue_declare(Q)
channel.queue_bind(Q, Q, Q)
"""

INIT_AMQPLIB = """
from amqplib import client_0_8 as amqp
Q = "amqplib.benchmark"
%s
""" % INIT_COMMON

INIT_LIBRABBIT = """
import librabbitmq as amqp

Q = "librabbit.benchmark"
%s
""" % INIT_COMMON

PUBLISH = """
message = amqp.Message("x" * %d)
channel.basic_publish(message, exchange=Q, routing_key=Q)
"""

PUBLISH_LIBRABBIT = """
connection._basic_publish(1, "x" * %d, Q, Q, {})
"""

CONSUME = """
method = getattr(channel, "wait", None) or connection.drain_events
def callback(m):
    channel.basic_ack(m.delivery_info["delivery_tag"])
channel.basic_consume(Q, callback=callback)
for i in range(%(its)d):
    method()
"""


def bench_basic_publish(iterations=10000, bytes=256):
    t_publish_amqplib = timeit.Timer(stmt=PUBLISH % bytes,
                                          setup=INIT_AMQPLIB)
    t_publish_librabbit = timeit.Timer(stmt=PUBLISH_LIBRABBIT % bytes,
                                       setup=INIT_LIBRABBIT)
    print("basic.publish: (%s byte messages)" % bytes)
    print("    amqplib:   %.2f usec/pass" % (
        iterations * t_publish_amqplib.timeit(number=iterations)/iterations))
    print("    librabbit: %.2f usec/pass" % (
        iterations * t_publish_librabbit.timeit(number=iterations)/iterations))

def bench_basic_consume(iterations=10000):
    context = {"its": (iterations/2)/10}
    t_consume_amqplib = timeit.Timer(stmt=CONSUME % context,
                                     setup=INIT_AMQPLIB)
    t_consume_librabbit = timeit.Timer(stmt=CONSUME % context,
                                       setup=INIT_LIBRABBIT)
    print("basic.consume (%s msg/pass) " % context["its"])
    print("    amqplib:   %.2f usec/pass" % (
        10 * t_consume_amqplib.timeit(number=10)/10))
    print("    librabbit: %.2f usec/pass" % (
        10 * t_consume_librabbit.timeit(number=10)/10))
benchmarks = [bench_basic_publish, bench_basic_consume]

if __name__ == "__main__":
    for benchmark in benchmarks:
        benchmark(100000)


