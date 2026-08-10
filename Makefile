PLATFORMIO_CORE_DIR := $(CURDIR)/.platformio
export PLATFORMIO_CORE_DIR

# This project has no managed ESP-IDF components. Disabling the optional
# component manager also keeps clean/configure builds self-contained.
IDF_COMPONENT_MANAGER := 0
export IDF_COMPONENT_MANAGER

PLATFORMIO := .venv/bin/pio
PYTHON := .venv/bin/python
BLESS_PACKAGE := .venv/lib/python3.13/site-packages/bless/__init__.py

.PHONY: setup build upload monitor serial mock-camera compile-commands clean

setup: $(PLATFORMIO) $(BLESS_PACKAGE)

$(PLATFORMIO):
	uv venv --python 3.13 .venv
	uv pip install --python .venv/bin/python pip platformio==6.1.18 pyserial==3.5

$(BLESS_PACKAGE): $(PLATFORMIO)
	uv pip install --python .venv/bin/python bless==0.3.0

build: setup
	$(PLATFORMIO) run

upload: setup
	$(PLATFORMIO) run --target upload

monitor: setup
	$(PLATFORMIO) device monitor

serial: setup
	$(PYTHON) tools/c6_serial.py $(ARGS)

mock-camera: setup
	$(PYTHON) tools/mock_canon_camera.py $(ARGS)

compile-commands: setup
	$(PLATFORMIO) run --target compiledb

clean: setup
	$(PLATFORMIO) run --target clean
