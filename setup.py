import os
import sys
from setuptools import setup, Extension

# --with-librabbitmq=<dir>: path to librabbitmq package if needed

cmd = None
pkgdirs = []  # incdirs and libdirs get these
libs = ["rabbitmq"]
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

for pkgdir in pkgdirs:
    incdirs.append(os.path.join(pkgdir, "include"))
    libdirs.append(os.path.join(pkgdir, "lib"))

pyrabbitmq_ext = Extension("_pyrabbitmq", ["pylibrabbitmq/_rabbitmqmodule.c"],
                        libraries=libs, include_dirs=incdirs,
                        library_dirs=libdirs, define_macros=defs)

# Hidden secret: if environment variable GEN_SETUP is set, generate Setup file.
if cmd == "gen-setup":
    line = " ".join((
        pyrabbitmq_ext.name,
        " ".join("-l" + lib for lib in pyrabbitmq_ext.libraries),
        " ".join("-I" + incdir for incdir in pyrabbitmq_ext.include_dirs),
        " ".join("-L" + libdir for libdir in pyrabbitmq_ext.library_dirs),
        " ".join("-D" + name + ("=" + str(value), "")[value is None] for
                (name, value) in pyrabbitmq_ext.define_macros)))
    open("Setup", "w").write(line + "\n")
    sys.exit(0)

long_description = open("README.rst", "U").read()
distmeta = open("pylibrabbitmq/pylibrabbitmq_distmeta.h").read().strip().splitlines()
distmeta = [item.split('\"')[1] for item in distmeta]
version = distmeta[0].strip()
author = distmeta[1].strip()
contact = distmeta[2].strip()
homepage = distmeta[3].strip()

setup(
    name="pylibrabbitmq",
    version=version,
    url=homepage,
    author=author,
    author_email=contact,
    license="MPL",
    description="Python bindings to librabbitmq-c",
    long_description=long_description,
    test_suite="nose.collector",
    zip_safe=False,
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
    ext_modules=[pyrabbitmq_ext], py_modules=["pylibrabbitmq"],
)
