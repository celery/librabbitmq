import os
import sys
from distutils.core import setup, Extension

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

pyrabbitmq_ext = Extension("_pyrabbitmq", ["_rabbitmqmodule.c"],
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

readme_text = "foo" #open("README.rst", "U").read()
version = "0.0.1" #open("pylibmc-version.h", "U").read().strip().split("\"")[1]

setup(name="pylibrabbitmq", version=version,
        url="http://github.com/ask/pylibrabbitmq",
      author="Ask Solem", author_email="ask@celeryproject.org",
      license="BSD",
      description="In-progress Python bindings to librabbitmq-c",
      long_description=readme_text,
      ext_modules=[pyrabbitmq_ext], py_modules=["pylibrabbitmq"])
