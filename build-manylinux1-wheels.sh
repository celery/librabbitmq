#!/bin/bash
# Script modified from https://github.com/pypa/python-manylinux-demo
set -e -x

# Install system packages required by our library
yum install -y cmake openssl-devel gcc automake

# Compile wheels
for PYBIN in /opt/python/*/bin; do
    # Ensure a fresh build of rabbitmq-c.
    (cd /workspace && PATH="${PYBIN}:${PATH}" make clean)
    (cd /workspace && "${PYBIN}"/python setup.py install)
    "${PYBIN}"/pip wheel /workspace/ -w wheelhouse/
done

# use a temporary directory to avoid picking up old wheels
WHEELHOUSE=/workspace/wheelhouse
TMP_WHEELHOUSE=$(mktemp -d -p "${WHEELHOUSE}")

# Bundle external shared libraries into the wheels
for whl in wheelhouse/*linux*.whl; do
    auditwheel repair "$whl" -w "${TMP_WHEELHOUSE}"
done

# Install packages and test
for PYBIN in /opt/python/*/bin/; do
    PYVER=$(echo "${PYBIN}" | cut -d'/' -f 4)

    # amqp 5.0.0a1 and vine 5.0.0a1 breaks python2
    # https://github.com/celery/vine/issues/34
    if [[ "${PYVER}" == *"cp2"* ]]; then
        "${PYBIN}"/pip install --force-reinstall "vine==1.3.0"
        "${PYBIN}"/pip install --force-reinstall "amqp==2.5.2"
    fi

    "${PYBIN}"/pip install librabbitmq --no-index -f "${TMP_WHEELHOUSE}"/*-"${PYVER}"-*.whl
    "${PYBIN}"/python -c "import librabbitmq"
    #(cd $HOME; ${PYBIN}/nosetests pymanylinuxdemo)
    mv -f "${TMP_WHEELHOUSE}"/*-"${PYVER}"-*.whl ${WHEELHOUSE}
done

rmdir "${TMP_WHEELHOUSE}"
