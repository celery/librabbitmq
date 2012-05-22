import os
import platform
import sys
from glob import glob
from setuptools import setup, find_packages

# --with-librabbitmq=<dir>: path to librabbitmq package if needed

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

    #for pkgdir in pkgdirs:
    #    incdirs.append(os.path.join(pkgdir, "include"))
    #    libdirs.append(os.path.join(pkgdir, "lib"))
    incdirs.append(os.path.join(os.path.abspath(
        os.getcwd()), "rabbitmq-c", "librabbitmq",
    ))
    libdirs.append(os.path.join(os.path.abspath(
        os.getcwd()), "rabbitmq-c", "librabbitmq", ".libs",
    ))

    librabbitmq_ext = Extension("_librabbitmq", ["librabbitmq/_rabbitmqmodule.c"],
                            libraries=libs, include_dirs=incdirs,
                            library_dirs=libdirs, define_macros=defs)

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

        def run(self):
            here = os.path.abspath(os.getcwd())
            H = lambda *x: os.path.join(here, *x)
            from distutils import sysconfig
            config = sysconfig.get_config_vars()
            try:
                restore = senv(("CFLAGS", config["CFLAGS"]),
                    ("LDFLAGS", config["LDFLAGS"]))
                try:
                    os.chdir(H("rabbitmq-c"))
                    if not os.path.isfile("config.h"):
                        print("- configure rabbitmq-c...")
                        os.system("/bin/sh %s --disable-dependency-tracking" % (
                            H("rabbitmq-c", "configure"), ))
                    print("- make rabbitmq-c...")
                    os.chdir(H("rabbitmq-c", "librabbitmq"))
                    os.system('"%s" all' % find_make())
                finally:
                    os.environ.update(restore)
            finally:
                os.chdir(here)
            os.environ["LDFLAGS"] = " ".join(["%s" % f
                for f in glob(H("rabbitmq-c", "librabbitmq", "*.o"))])
            _build.run(self)
    return librabbitmq_ext, build


def find_make(alt=("gmake", "gnumake", "make", "nmake")):
    for path in os.environ["PATH"].split(":"):
        for make in (os.path.join(path, m) for m in alt):
            if os.path.isfile(make):
                return make


def senv(*k__v):
    restore = {}
    for k, v in k__v:
        prev = restore[k] = os.environ.get(k)
        os.environ[k] = (prev + " " if prev else "") + str(v)
    return dict((k, v) for k, v in restore.iteritems() if v is not None)


long_description = open("README.rst", "U").read()
distmeta = open("librabbitmq/librabbitmq_distmeta.h").read().strip().splitlines()
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
    except Exception:
        pass
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
