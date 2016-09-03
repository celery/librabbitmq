#!/bin/bash
# Script modified from https://github.com/pypa/python-manylinux-demo
set -e -x

# Install a system package required by our library
yum  install -y librabbmitmq-devel make librabbitmq python-dev gcc automake


# Compile wheels
for PYBIN in /opt/python/*/bin; do
    #${PYBIN}/pip install -r /workspace/dev-requirements.txt
    ${PYBIN}/pip wheel /workspace/ -w wheelhouse/
done

# Bundle external shared libraries into the wheels
#ls wheelhouse/*
for whl in wheelhouse/*linux*.whl; do
    auditwheel repair $whl -w /workspace/wheelhouse/
done

# Install packages and test
for PYBIN in /opt/python/*/bin/; do
    ${PYBIN}/pip install librabbitmq -f /workspace/wheelhouse
    ${PYBIN}/python -c "import librabbitmq"
    #(cd $HOME; ${PYBIN}/nosetests pymanylinuxdemo)
done
