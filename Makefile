PLATFORMIO_CORE_DIR := $(CURDIR)/.platformio
export PLATFORMIO_CORE_DIR

PLATFORMIO := .venv/bin/pio

.PHONY: setup build upload monitor clean

setup: $(PLATFORMIO)

$(PLATFORMIO):
	uv venv --python 3.13 .venv
	uv pip install --python .venv/bin/python pip platformio==6.1.18

build: setup
	$(PLATFORMIO) run

upload: setup
	$(PLATFORMIO) run --target upload

monitor: setup
	$(PLATFORMIO) device monitor

clean: setup
	$(PLATFORMIO) run --target clean
