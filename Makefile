# Building
RABBIT_DIR=rabbitmq-c
RABBIT_DIST=librabbitmq

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
	$(PYTHON) setup.py build

install: build
	$(PYTHON) setup.py install

develop: build
	$(PYTHON) setup.py develop

pyclean:
	-$(PYTHON) setup.py clean
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


dist: rabbitmq-c $(RABBIT_TARGET)

manylinux1: dist
	 docker run --rm -v `pwd`:/workspace:z quay.io/pypa/manylinux1_x86_64  /workspace/build-manylinux1-wheels.sh

rebuild:
	$(PYTHON) setup.py build
	$(PYTHON) setup.py install


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

test: build
	$(PYTHON) setup.py test

cov: build
	coverage run --source=librabbitmq setup.py test
	coverage report

removepyc:
	-find . -type f -a \( -name "*.pyc" -o -name "*$$py.class" \) | xargs rm
	-find . -type d -name "__pycache__" | xargs rm -r

gitclean:
	git clean -xdn

gitcleanforce:
	git clean -xdf

distcheck: flakecheck test gitclean
