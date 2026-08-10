ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
TARGET_FILE := $(ROOT)/.target
SUPPORTED_TARGETS := esp32c6 ch582m
SAVED_TARGET := $(strip $(shell test -f "$(TARGET_FILE)" && sed -n '1p' "$(TARGET_FILE)"))
TARGET ?= $(if $(SAVED_TARGET),$(SAVED_TARGET),esp32c6)

ifeq ($(filter $(TARGET),$(SUPPORTED_TARGETS)),)
$(error Unsupported TARGET '$(TARGET)'; choose one of: $(SUPPORTED_TARGETS))
endif

PLATFORMIO_CORE_DIR := $(ROOT)/.platformio
IDF_COMPONENT_MANAGER := 0
export PLATFORMIO_CORE_DIR IDF_COMPONENT_MANAGER

PLATFORMIO := $(ROOT)/.venv/bin/pio
PYTHON := $(ROOT)/.venv/bin/python
ESP32C6_DIR := $(ROOT)/firmware/esp32c6
CH582M_DIR := $(ROOT)/firmware/ch582m
BUILD_ROOT := $(ROOT)/.build

CH582M_SDK_ROOT := $(ROOT)/.cache/ch58x-sdk
CH582M_SDK_MARKER := $(CH582M_SDK_ROOT)/.sdk-commit
CH582M_TOOLCHAIN_BIN := $(ROOT)/.platformio/packages/toolchain-riscv32-esp/bin
CH582M_GCC := $(CH582M_TOOLCHAIN_BIN)/riscv32-esp-elf-gcc
CH582M_CMAKE := $(ROOT)/.platformio/packages/tool-cmake/bin/cmake
CH582M_NINJA := $(ROOT)/.platformio/packages/tool-ninja/ninja
CH582M_BUILD_DIR := $(BUILD_ROOT)/ch582m

HOST_CC ?= cc
HOST_TEST := $(BUILD_ROOT)/host/test_canon_protocol

.PHONY: help target select setup build upload monitor serial test \
        compile-commands clean clean-all ch582m-configure

help:
	@echo "Selected target: $(TARGET)"
	@echo ""
	@echo "  make select TARGET=esp32c6  Save the active target"
	@echo "  make select TARGET=ch582m   Save the active target"
	@echo "  make build                  Build the active target"
	@echo "  make upload                 Flash the active target"
	@echo "  make serial                 Open its UART terminal"
	@echo "  make test                   Run platform-independent tests"
	@echo "  make compile-commands       Refresh clangd/IntelliSense data"
	@echo ""
	@echo "TARGET=<name> can override the saved target for one command."

target:
	@echo "$(TARGET)"

select:
	@printf '%s\n' "$(TARGET)" > "$(TARGET_FILE)"
	@echo "Selected target: $(TARGET)"

$(PLATFORMIO):
	uv venv --python 3.13 "$(ROOT)/.venv"
	uv pip install --python "$(PYTHON)" pip platformio==6.1.18 pyserial==3.5

$(CH582M_GCC): | $(PLATFORMIO)
	$(PLATFORMIO) pkg install --global --tool "platformio/toolchain-riscv32-esp@15.2.0+20251204"

$(CH582M_CMAKE): | $(PLATFORMIO)
	$(PLATFORMIO) pkg install --global --tool "platformio/tool-cmake@3.30.2"

$(CH582M_NINJA): | $(PLATFORMIO)
	$(PLATFORMIO) pkg install --global --tool "platformio/tool-ninja@1.13.2"

$(CH582M_SDK_MARKER): tools/setup_ch582m_sdk.py
	python3 tools/setup_ch582m_sdk.py "$(CH582M_SDK_ROOT)"

ifeq ($(TARGET),esp32c6)

setup: $(PLATFORMIO)

build: setup
	$(PLATFORMIO) run --project-dir "$(ESP32C6_DIR)"

upload: setup
	$(PLATFORMIO) run --project-dir "$(ESP32C6_DIR)" --target upload

monitor: setup
	$(PLATFORMIO) device monitor --project-dir "$(ESP32C6_DIR)"

serial: setup
	SERIAL_TARGET=esp32c6 $(PYTHON) tools/serial_terminal.py $(ARGS)

compile-commands: setup
	$(PLATFORMIO) run --project-dir "$(ESP32C6_DIR)" --target compiledb
	ln -sfn "$(ESP32C6_DIR)/compile_commands.json" "$(ROOT)/compile_commands.json"

clean: setup
	$(PLATFORMIO) run --project-dir "$(ESP32C6_DIR)" --target clean

else

setup: $(PLATFORMIO) $(CH582M_GCC) $(CH582M_CMAKE) $(CH582M_NINJA) \
       $(CH582M_SDK_MARKER)

ch582m-configure: setup
	"$(CH582M_CMAKE)" -S "$(CH582M_DIR)" -B "$(CH582M_BUILD_DIR)" \
		-G Ninja \
		-DCMAKE_MAKE_PROGRAM="$(CH582M_NINJA)" \
		-DCH58X_SDK_ROOT="$(CH582M_SDK_ROOT)" \
		-DCH58X_TOOLCHAIN_BIN="$(CH582M_TOOLCHAIN_BIN)" \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON

build: ch582m-configure
	"$(CH582M_CMAKE)" --build "$(CH582M_BUILD_DIR)"

upload: build
	@if command -v "$${CH582M_FLASHER:-wchisp}" >/dev/null 2>&1; then \
		"$${CH582M_FLASHER:-wchisp}" flash "$(CH582M_BUILD_DIR)/ch582m_canon_remote.bin"; \
	else \
		echo "CH582M flashing needs wchisp (USB ISP) or CH582M_FLASHER=<tool>."; \
		echo "The current CH582M target is compile-verified but awaits hardware testing."; \
		exit 2; \
	fi

monitor serial: setup
	SERIAL_TARGET=ch582m $(PYTHON) tools/serial_terminal.py $(ARGS)

compile-commands: ch582m-configure
	ln -sfn "$(CH582M_BUILD_DIR)/compile_commands.json" "$(ROOT)/compile_commands.json"

clean:
	$(RM) -r "$(CH582M_BUILD_DIR)"

endif

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
	$(RM) -r "$(BUILD_ROOT)" "$(ESP32C6_DIR)/.pio"
