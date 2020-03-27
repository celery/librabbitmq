#!/bin/bash
# Script modified from https://github.com/pypa/python-manylinux-demo
set -e -x

# Install system packages required by our library
yum install -y cmake openssl-devel gcc automake

# Compile wheels
for PYBIN in /opt/python/*/bin; do
    # cp27-cp27mu builds fail with: undefined symbol: PyUnicodeUCS2_AsASCIIString
    if [[ "${PYBIN}" == *"cp27-cp27mu"* ]]; then
        continue
    fi

    # Ensure a fresh build of rabbitmq-c.
    rm -rf /workspace/rabbitmq-c/build
    (cd /workspace && ${PYBIN}/python setup.py install)
    ${PYBIN}/pip wheel /workspace/ -w wheelhouse/
done

# Bundle external shared libraries into the wheels
for whl in wheelhouse/*linux*.whl; do
    auditwheel repair $whl -w /workspace/wheelhouse/
done

# Install packages and test
for PYBIN in /opt/python/*/bin/; do
    if [[ "${PYBIN}" == *"cp27-cp27mu"* ]]; then
        continue
    fi

    # amqp 5.0.0a1 and vine 5.0.0a1 breaks python2
    # https://github.com/celery/vine/issues/34
    if [[ "${PYBIN}" == *"python/cp2"* ]]; then
        ${PYBIN}/pip install --force-reinstall "vine==1.3.0"
        ${PYBIN}/pip install --force-reinstall "amqp==2.5.2"
    fi

    ${PYBIN}/pip install librabbitmq -f /workspace/wheelhouse
    ${PYBIN}/python -c "import librabbitmq"
    #(cd $HOME; ${PYBIN}/nosetests pymanylinuxdemo)
done
