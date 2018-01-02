import timeit

INIT_COMMON = """
connection = amqp.Connection(hostname="localhost", userid="guest",
password="guest", virtual_host="/", lazy=True)
connection.connect()
channel = connection.channel()
channel.exchange_declare(Q, "direct")
channel.queue_declare(Q)
channel.queue_bind(Q, Q, Q)
"""

INIT_AMQP = """
import amqp
Q = "amqp.benchmark"
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
method = connection.drain_events
def callback(m):
    channel.basic_ack(m.delivery_info["delivery_tag"])
channel.basic_consume(Q, callback=callback)
for i in range(%(its)d):
    method()
"""


def bench_basic_publish(iterations=10000, bytes=256):
    t_publish_amqp = timeit.Timer(stmt=PUBLISH % bytes,
                                  setup=INIT_AMQP)
    t_publish_librabbit = timeit.Timer(stmt=PUBLISH_LIBRABBIT % bytes,
                                       setup=INIT_LIBRABBIT)
    print("basic.publish: (%s x %s bytes messages)" % (iterations, bytes))
    print("    amqp:   %.2f sec/pass" % (
        iterations * t_publish_amqp.timeit(number=iterations)/iterations)
    )
    print("    librabbit: %.2f sec/pass" % (
        iterations * t_publish_librabbit.timeit(number=iterations)/iterations)
    )

def bench_basic_consume(iterations=10000, bytes=None):
    context = {"its": (iterations/2)/10}
    t_consume_amqp = timeit.Timer(stmt=CONSUME % context,
                                  setup=INIT_AMQP)
    t_consume_librabbit = timeit.Timer(stmt=CONSUME % context,
                                       setup=INIT_LIBRABBIT)
    print("basic.consume (%s msg/pass) " % context["its"])
    print("    amqp:   %.2f sec/pass" % (
        t_consume_amqp.timeit(number=10))
    )
    print("    librabbit: %.2f sec/pass" % (
        t_consume_librabbit.timeit(number=10))
    )

benchmarks = [bench_basic_publish, bench_basic_consume]

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description='Runs benchmark against local RabbitMQ instance.')
    parser.add_argument('--iters', metavar='N', type=int, default=100000,
                        help='Number of iterations')
    parser.add_argument('--bytes', metavar='B', type=int,
                        default=256, help='Message size')

    args = parser.parse_args()
    for benchmark in benchmarks:
        benchmark(args.iters, bytes=args.bytes)


