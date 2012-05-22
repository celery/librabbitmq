RABBIT_DIR=rabbitmq-c


all: build

add-submodules:
	-git submodule add https://github.com/alanxz/rabbitmq-c
	-git submodule add https://github.com/rabbitmq/rabbitmq-codegen

submodules:
	git submodule init
	git submodule update

rabbitmq-c: submodules
	(cd $(RABBIT_DIR); test -f configure || autoreconf -i)
	(cd $(RABBIT_DIR); test -f Makefile  || automake --add-missing)

rabbitmq-clean:
	-(cd $(RABBIT_DIR); make clean)

rabbitmq-distclean:
	-(cd $(RABBIT_DIR); make distclean)

clean-build:
	-rm -rf build

build: clean-build rabbitmq-c
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

distclean: pyclean rabbitmq-distclean
	-rm -rf dist


dist: distclean rabbitmq-c
