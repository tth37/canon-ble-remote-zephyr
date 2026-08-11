ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
PLATFORMIO_CORE_DIR := $(ROOT)/.platformio
IDF_COMPONENT_MANAGER := 0
export PLATFORMIO_CORE_DIR IDF_COMPONENT_MANAGER

PLATFORMIO := $(ROOT)/.venv/bin/pio
PYTHON := $(ROOT)/.venv/bin/python
FIRMWARE_DIR := $(ROOT)/firmware/esp32c6
BUILD_ROOT := $(ROOT)/.build
HOST_CC ?= cc
HOST_TEST := $(BUILD_ROOT)/host/test_canon_protocol

.PHONY: help setup build upload monitor serial test compile-commands clean \
        clean-all

help:
	@echo "ESP32-C6 Canon BLE remote (ESP-IDF/NimBLE)"
	@echo ""
	@echo "  make setup             Install the local PlatformIO environment"
	@echo "  make build             Build the firmware"
	@echo "  make upload            Flash the connected ESP32-C6"
	@echo "  make serial            Open the interactive serial terminal"
	@echo "  make test              Run the portable Canon protocol tests"
	@echo "  make compile-commands  Refresh clangd/IntelliSense data"

$(PLATFORMIO):
	uv venv --python 3.13 "$(ROOT)/.venv"
	uv pip install --python "$(PYTHON)" pip platformio==6.1.18 pyserial==3.5

setup: $(PLATFORMIO)

build: setup
	$(PLATFORMIO) run --project-dir "$(FIRMWARE_DIR)"

upload: setup
	$(PLATFORMIO) run --project-dir "$(FIRMWARE_DIR)" --target upload

monitor: setup
	$(PLATFORMIO) device monitor --project-dir "$(FIRMWARE_DIR)"

serial: setup
	SERIAL_TARGET=esp32c6 $(PYTHON) tools/serial_terminal.py $(ARGS)

compile-commands: setup
	$(PLATFORMIO) run --project-dir "$(FIRMWARE_DIR)" --target compiledb
	ln -sfn "$(FIRMWARE_DIR)/compile_commands.json" \
		"$(ROOT)/compile_commands.json"

clean: setup
	$(PLATFORMIO) run --project-dir "$(FIRMWARE_DIR)" --target clean

$(HOST_TEST): tests/host/test_canon_protocol.c \
              shared/canon/src/canon_protocol.c \
              shared/canon/include/canon_protocol.h
	mkdir -p "$(dir $(HOST_TEST))"
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -pedantic \
		-Ishared/canon/include \
		tests/host/test_canon_protocol.c shared/canon/src/canon_protocol.c \
		-o "$(HOST_TEST)"

test: $(HOST_TEST)
	"$(HOST_TEST)"

clean-all:
	$(RM) -r "$(BUILD_ROOT)" "$(FIRMWARE_DIR)/.pio"
