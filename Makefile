# Building
RABBIT_DIR=rabbitmq-c
RABBIT_TARGET=clib
RABBIT_DIST=rabbitmq-c-0.8.0

# Distribuition tools
PYTHON=python

all: build

add-submodules:
	-git submodule add -b v0.8.0 https://github.com/alanxz/rabbitmq-c.git

submodules:
	git submodule init
	git submodule update

rabbitmq-c: submodules
	(cd $(RABBIT_DIR); test -f configure || autoreconf -i)
	(cd $(RABBIT_DIR); test -f Makefile  || automake --add-missing)


rabbitmq-clean:
	-(cd $(RABBIT_DIR) && make clean)

rabbitmq-distclean:
	-(cd $(RABBIT_DIR) && make distclean)

clean-build:
	-rm -rf build

build: clean-build dist
	python setup.py build

install: build
	python setup.py install

develop: build
	python setup.py develop

pyclean:
	-python setup.py clean
	-rm -rf build
	-rm -f _librabbitmq.so

clean: pyclean rabbitmq-clean

distclean: pyclean rabbitmq-distclean removepyc
	-rm -rf dist
	-rm -rf clib
	-rm -f erl_crash.dump

$(RABBIT_TARGET):
	(test -f config.h || cd $(RABBIT_DIR); ./configure --disable-tools --disable-docs)
	(cd $(RABBIT_DIR); make)
	mv "$(RABBIT_DIR)/$(RABBIT_DIST)" "$(RABBIT_TARGET)"


dist: rabbitmq-c $(RABBIT_TARGET)

manylinux1: dist
	 docker run --rm -v `pwd`:/workspace:z quay.io/pypa/manylinux1_x86_64  /workspace/build-manylinux1-wheels.sh

rebuild:
	python setup.py build
	python setup.py install


# Distro tools

flakecheck:
	flake8 librabbitmq setup.py

flakediag:
	-$(MAKE) flakecheck

flakepluscheck:
	flakeplus librabbitmq

flakeplusdiag:
	-$(MAKE) flakepluscheck

flakes: flakediag flakeplusdiag

test:
	nosetests -xv librabbitmq.tests

cov:
	nosetests -xv librabbitmq.tests --with-coverage --cover-html --cover-branch

removepyc:
	-find . -type f -a \( -name "*.pyc" -o -name "*$$py.class" \) | xargs rm
	-find . -type d -name "__pycache__" | xargs rm -r

gitclean:
	git clean -xdn

gitcleanforce:
	git clean -xdf

distcheck: flakecheck test gitclean
