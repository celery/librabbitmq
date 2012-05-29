import os
import platform
import sys
from glob import glob
from setuptools import setup, find_packages

# --with-librabbitmq=<dir>: path to librabbitmq package if needed


LRMQDIST = lambda *x: os.path.join("clib", *x)
LRMQSRC = lambda *x: LRMQDIST("librabbitmq", *x)
SPECPATH = lambda *x: os.path.join("rabbitmq-codegen", *x)
PYCP = lambda *x: os.path.join("Modules", "_librabbitmq", *x)



def senv(*k__v, **kwargs):
    sep = kwargs.get("sep", ' ')
    restore = {}
    for k, v in k__v:
        prev = restore[k] = os.environ.get(k)
        os.environ[k] = (prev + sep if prev else "") + str(v)
    return dict((k, v) for k, v in restore.iteritems() if v is not None)


def codegen():
    codegen = LRMQSRC("codegen.py")
    spec = SPECPATH("amqp-rabbitmq-0.9.1.json")
    sys.path.insert(0, SPECPATH())
    commands = [
        (sys.executable, codegen, "header", spec, LRMQSRC("amqp_framing.h")),
        (sys.executable, codegen, "body", spec, LRMQSRC("amqp_framing.c")),
    ]
    restore = senv(("PYTHONPATH", SPECPATH()), sep=':')
    try:
        for command in commands:
            print("- generating %r" % command[-1])
            print(" ".join(command))
            os.system(" ".join(command))
    finally:
        os.environ.update(restore)



def create_builder():
    from setuptools import Extension
    from distutils.command.build import build as _build
    cmd = None
    pkgdirs = []  # incdirs and libdirs get these
    libs = []#"rabbitmq"]
    defs = []
    incdirs = []
    libdirs = []

    def append_env(L, e):
        v = os.environ.get(e)
        if v and os.path.exists(v):
            L.append(v)

    append_env(pkgdirs, "LIBRABBITMQ")

    # Hack up sys.argv, yay
    unprocessed = []
    for arg in sys.argv[1:]:
        if arg == "--gen-setup":
            cmd = arg[2:]
        elif "=" in arg:
            if arg.startswith("--with-librabbitmq="):
                pkgdirs.append(arg.split("=", 1)[1])
                continue
        unprocessed.append(arg)
    sys.argv[1:] = unprocessed

    incdirs.append(LRMQSRC())
    PyC_files = map(PYCP, [
        "connection.c",
    ])
    librabbit_files = map(LRMQSRC, [
        "amqp_api.c",
        "amqp_mem.c",
        "amqp_url.c",
        "amqp_connection.c",
        "amqp_socket.c",
        "amqp_framing.c",
        "amqp_table.c",
    ])

    incdirs.append(LRMQDIST())  # for config.h
    if platform.system() == 'Windows':
        incdirs.append(LRMQSRC("windows"))
        librabbit_files.append(LRMQSRC("windows", "socket.c"))
    else:
        incdirs.append(LRMQSRC("unix"))
        librabbit_files.append(LRMQSRC("unix", "socket.c"))

    librabbitmq_ext = Extension("_librabbitmq",
                            sources=PyC_files + librabbit_files,
                            libraries=libs, include_dirs=incdirs,
                            library_dirs=libdirs, define_macros=defs)
                            #depends=(glob(PYCP("*.h")) + ["setup.py"]))

    # Hidden secret: if environment variable GEN_SETUP is set, generate Setup file.
    if cmd == "gen-setup":
        line = " ".join((
            librabbitmq_ext.name,
            " ".join("-l" + lib for lib in librabbitmq_ext.libraries),
            " ".join("-I" + incdir for incdir in librabbitmq_ext.include_dirs),
            " ".join("-L" + libdir for libdir in librabbitmq_ext.library_dirs),
            " ".join("-D" + name + ("=" + str(value), "")[value is None] for
                    (name, value) in librabbitmq_ext.define_macros)))
        open("Setup", "w").write(line + "\n")
        sys.exit(0)

    class build(_build):
        stdcflags = [
            "-W -Wall -ansi",
        ]
        if os.environ.get("PEDANTIC"):
            # Python.h breaks -pedantic, so can only use it while developing.
            stdcflags.append("-pedantic -Werror")

        def run(self):
            here = os.path.abspath(os.getcwd())
            H = lambda *x: os.path.join(here, *x)
            from distutils import sysconfig
            config = sysconfig.get_config_vars()
            try:
                restore = senv(("CFLAGS", config["CFLAGS"]),
                    ("LDFLAGS", config["LDFLAGS"]))
                try:
                    os.chdir(LRMQDIST())
                    if not os.path.isfile("config.h"):
                        print("- configure rabbitmq-c...")
                        os.system("/bin/sh configure --disable-dependency-tracking")
                    #print("- make rabbitmq-c...")
                    #os.chdir(LRMQSRC())
                    #os.system('"%s" all' % find_make())
                finally:
                    os.environ.update(restore)
            finally:
                os.chdir(here)
            restore = senv(
                #("LDFLAGS", ' '.join(glob(LRMQSRC("*.o")))),
                ("CFLAGS", ' '.join(self.stdcflags)),
            )
            codegen()
            try:
                _build.run(self)
            finally:
                os.environ.update(restore)
    return librabbitmq_ext, build


def find_make(alt=("gmake", "gnumake", "make", "nmake")):
    for path in os.environ["PATH"].split(":"):
        for make in (os.path.join(path, m) for m in alt):
            if os.path.isfile(make):
                return make


long_description = open("README.rst", "U").read()
distmeta = open(PYCP("distmeta.h")).read().strip().splitlines()
distmeta = [item.split('\"')[1] for item in distmeta]
version = distmeta[0].strip()
author = distmeta[1].strip()
contact = distmeta[2].strip()
homepage = distmeta[3].strip()



ext_modules = []
cmdclass = {}
packages = []
install_requires = []
goahead = False
is_jython = sys.platform.startswith("java")
is_pypy = hasattr(sys, "pypy_version_info")
is_py3k = sys.version_info[0] == 3
is_win = platform.system() == "Windows"
if is_jython or is_pypy or is_py3k or is_win:
    pass
elif find_make():
    try:
        librabbitmq_ext, build = create_builder()
    except Exception, exc:
        raise
        print("Couldn't create builder: %r" % (exc, ))
    else:
        goahead = True
        ext_modules= [librabbitmq_ext]
        cmdclass = {"build": build}
        packages = find_packages(exclude=['ez_setup', 'tests', 'tests.*'])

if not goahead:
    install_requires = ["amqplib>=1.0.2"]
    ext_modules = []
    cmdclass = {}
    packages = []

setup(
    name="librabbitmq",
    version=version,
    url=homepage,
    author=author,
    author_email=contact,
    license="MPL",
    description="AMQP Client using the rabbitmq-c library.",
    long_description=long_description,
    test_suite="nose.collector",
    zip_safe=False,
    packages=packages,
    cmdclass=cmdclass,
    install_requires=install_requires,
    ext_modules=ext_modules,
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Operating System :: OS Independent",
        "Programming Language :: C",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: Mozilla Public License 1.0 (MPL)",
        "Topic :: Communications",
        "Topic :: System :: Networking",
        "Topic :: Software Development :: Libraries",
    ],
)
