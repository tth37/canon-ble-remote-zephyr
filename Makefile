PLATFORMIO_CORE_DIR := $(CURDIR)/.platformio
export PLATFORMIO_CORE_DIR

PLATFORMIO := .venv/bin/pio
PYTHON := .venv/bin/python

.PHONY: setup build upload monitor serial compile-commands clean

setup: $(PLATFORMIO)

$(PLATFORMIO):
	uv venv --python 3.13 .venv
	uv pip install --python .venv/bin/python pip platformio==6.1.18 pyserial==3.5

build: setup
	$(PLATFORMIO) run

upload: setup
	$(PLATFORMIO) run --target upload

monitor: setup
	$(PLATFORMIO) device monitor

serial: setup
	$(PYTHON) tools/c6_serial.py $(ARGS)

compile-commands: setup
	$(PLATFORMIO) run --target compiledb

clean: setup
	$(PLATFORMIO) run --target clean
