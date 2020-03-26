#!/bin/bash
# Script modified from https://github.com/pypa/python-manylinux-demo
set -e -x

# Install system packages required by our library
yum install -y cmake openssl-devel gcc automake

# Compile wheels
for PYBIN in /opt/python/*/bin; do
    (cd /workspace && ${PYBIN}/python setup.py install)
    ${PYBIN}/pip wheel /workspace/ -w wheelhouse/
done

# Bundle external shared libraries into the wheels
#ls wheelhouse/*
for whl in wheelhouse/*linux*.whl; do
    auditwheel repair $whl -w /workspace/wheelhouse/
done

# Install packages and test
for PYBIN in /opt/python/*/bin/; do
    # vine 5.0.0a1 breaks python2
    # https://github.com/celery/vine/issues/34
    if [[ "$PYBIN" == *"python/cp2"* ]]; then
        ${PYBIN}/pip install -f "vine==1.3.0"
    fi

    ${PYBIN}/pip install librabbitmq -f /workspace/wheelhouse
    ${PYBIN}/python -c "import librabbitmq"
    #(cd $HOME; ${PYBIN}/nosetests pymanylinuxdemo)
done
