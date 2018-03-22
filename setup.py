import os
import platform
import sys
from setuptools import setup, find_packages

# --with-librabbitmq=<dir>: path to librabbitmq package if needed

BASE_PATH = os.path.dirname(__file__)

LRMQDIST = lambda *x: os.path.join(BASE_PATH, 'rabbitmq-c', *x)
LRMQSRC = lambda *x: LRMQDIST('librabbitmq', *x)
PYCP = lambda *x: os.path.join(BASE_PATH, 'Modules', '_librabbitmq', *x)


def senv(*k__v, **kwargs):
    sep = kwargs.get('sep', ' ')
    restore = {}
    for k, v in k__v:
        prev = restore[k] = os.environ.get(k)
        os.environ[k] = (prev + sep if prev else '') + str(v)
    return dict((k, v) for k, v in restore.items() if v is not None)


def create_builder():
    from setuptools import Extension
    from distutils.command.build import build as _build
    cmd = None
    pkgdirs = []  # incdirs and libdirs get these
    libs = []
    defs = []
    incdirs = []
    libdirs = []

    def append_env(L, e):
        v = os.environ.get(e)
        if v and os.path.exists(v):
            L.append(v)

    append_env(pkgdirs, 'LIBRABBITMQ')

    # Hack up sys.argv, yay
    unprocessed = []
    for arg in sys.argv[1:]:
        if arg == '--gen-setup':
            cmd = arg[2:]
        elif '=' in arg:
            if arg.startswith('--with-librabbitmq='):
                pkgdirs.append(arg.split('=', 1)[1])
                continue
        unprocessed.append(arg)
    sys.argv[1:] = unprocessed

    incdirs.append(LRMQSRC())
    PyC_files = map(PYCP, [
        'connection.c',
    ])
    librabbit_files = map(LRMQSRC, [
        'amqp_api.c',
        'amqp_connection.c',
        'amqp_consumer.c',
        'amqp_framing.c',
        'amqp_hostcheck.c',
        'amqp_mem.c',
        'amqp_socket.c',
        'amqp_table.c',
        'amqp_tcp_socket.c',
        'amqp_time.c',
        'amqp_url.c',
    ])

    incdirs.append(LRMQDIST())  # for config.h

    if is_linux:  # Issue #42
        libs.append('rt')  # -lrt for clock_gettime

    librabbitmq_ext = Extension(
        '_librabbitmq',
        sources=list(PyC_files) + list(librabbit_files),
        libraries=libs, include_dirs=incdirs,
        library_dirs=libdirs, define_macros=defs,
    )

    # Hidden secret:
    # If environment variable GEN_SETUP is set, generate Setup file.
    if cmd == 'gen-setup':
        line = ' '.join((
            librabbitmq_ext.name,
            ' '.join('-l' + lib for lib in librabbitmq_ext.libraries),
            ' '.join('-I' + incdir for incdir in librabbitmq_ext.include_dirs),
            ' '.join('-L' + libdir for libdir in librabbitmq_ext.library_dirs),
            ' '.join('-D' + name + ('=' + str(value), '')[value is None]
                     for name, value in librabbitmq_ext.define_macros)))
        open('Setup', 'w').write(line + '\n')
        sys.exit(0)

    class build(_build):
        stdcflags = [
            '-DHAVE_CONFIG_H',
        ]
        if os.environ.get('PEDANTIC'):
            # Python.h breaks -pedantic, so can only use it while developing.
            stdcflags.append('-pedantic -Werror')

        def run(self):
            from distutils import sysconfig

            here = os.path.abspath(os.getcwd())
            config = sysconfig.get_config_vars()
            make = find_make()

            try:
                vars = {'ld': config['LDFLAGS'],
                        'c': config['CFLAGS']}
                for key in list(vars):
                    vars[key] = vars[key].replace('-lSystem', '')
                    # Python on Maverics sets this, but not supported on clang
                    vars[key] = vars[key].replace('-mno-fused-madd', '')
                    vars[key] = vars[key].replace(
                        '-isysroot /Developer/SDKs/MacOSX10.6.sdk', '')
                restore = senv(
                    ('CFLAGS', vars['c']),
                    ('LDFLAGS', vars['ld']),
                )

                try:
                    if not os.path.isdir(os.path.join(LRMQDIST(), '.git')):
                        print('- pull submodule rabbitmq-c...')
                        if os.path.isfile('Makefile'):
                            os.system(' '.join([make, 'submodules']))
                        else:
                            os.system(' '.join(['git', 'clone', '-b', 'v0.8.0', 
                                'https://github.com/alanxz/rabbitmq-c.git',
                                'rabbitmq-c']))

                    os.chdir(LRMQDIST())

                    if not os.path.isfile('configure'):
                        print('- autoreconf')
                        os.system('autoreconf -i')

                    if not os.path.isfile('config.h'):
                        print('- configure rabbitmq-c...')
                        if os.system('/bin/sh configure --disable-tools \
                                --disable-docs --disable-dependency-tracking'):
                            return
                finally:
                    os.environ.update(restore)
            finally:
                os.chdir(here)
            restore = senv(
                ('CFLAGS', ' '.join(self.stdcflags)),
            )
            try:
                _build.run(self)
            finally:
                os.environ.update(restore)
    return librabbitmq_ext, build


def find_make(alt=('gmake', 'gnumake', 'make', 'nmake')):
    for path in os.environ['PATH'].split(':'):
        for make in (os.path.join(path, m) for m in alt):
            if os.path.isfile(make):
                return make


long_description = open(os.path.join(BASE_PATH, 'README.rst'), 'U').read()
distmeta = open(PYCP('distmeta.h')).read().strip().splitlines()
distmeta = [item.split('\"')[1] for item in distmeta]
version = distmeta[0].strip()
author = distmeta[1].strip()
contact = distmeta[2].strip()
homepage = distmeta[3].strip()

ext_modules = []
cmdclass = {}
packages = []
goahead = False
is_jython = sys.platform.startswith('java')
is_pypy = hasattr(sys, 'pypy_version_info')
is_win = platform.system() == 'Windows'
is_linux = platform.system() == 'Linux'
if is_jython or is_pypy or is_win:
    pass
elif find_make():
    try:
        librabbitmq_ext, build = create_builder()
    except Exception as exc:
        print('Could not create builder: %r' % (exc, ))
        raise
    else:
        goahead = True
        ext_modules = [librabbitmq_ext]
        cmdclass = {'build': build}
        packages = find_packages(exclude=['ez_setup', 'tests', 'tests.*'])

if not goahead:
    ext_modules = []
    cmdclass = {}
    packages = []


# 'install doesn't always call build for some reason
if 'install' in sys.argv and 'build' not in sys.argv:
    _index = sys.argv.index('install')
    sys.argv[:] = (
        sys.argv[:_index] + ['build', 'install'] + sys.argv[_index + 1:]
    )

# 'bdist_wheel doesn't always call build for some reason
if 'bdist_wheel' in sys.argv and 'build' not in sys.argv:
    _index = sys.argv.index('bdist_wheel')
    sys.argv[:] = (
        sys.argv[:_index] + ['build', 'bdist_wheel'] + sys.argv[_index + 1:]
    )

# 'bdist_egg doesn't always call build for some reason
if 'bdist_egg' in sys.argv and 'build' not in sys.argv:
    _index = sys.argv.index('bdist_egg')
    sys.argv[:] = (
        sys.argv[:_index] + ['build', 'bdist_egg'] + sys.argv[_index + 1:]
    )

# 'test doesn't always call build for some reason
if 'test' in sys.argv and 'build' not in sys.argv:
    _index = sys.argv.index('test')
    sys.argv[:] = (
        sys.argv[:_index] + ['build', 'test'] + sys.argv[_index + 1:]
    )

setup(
    name='librabbitmq',
    version=version,
    url=homepage,
    author=author,
    author_email=contact,
    license='MPL',
    description='AMQP Client using the rabbitmq-c library.',
    long_description=long_description,
    test_suite="tests",  
    zip_safe=False,
    packages=packages,
    cmdclass=cmdclass,
    install_requires=[
        'amqp>=1.4.6',
        'six>=1.0.0',
    ],
    ext_modules=ext_modules,
    classifiers=[
        'Development Status :: 5 - Production/Stable',
        'Operating System :: POSIX',
        'Operating System :: Microsoft :: Windows',
        'Programming Language :: C',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3.4',
        'Programming Language :: Python :: 3.5',
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: Implementation :: CPython',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: Mozilla Public License 1.0 (MPL)',
        'Topic :: Communications',
        'Topic :: System :: Networking',
        'Topic :: Software Development :: Libraries',
    ],
)
