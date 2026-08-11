ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
TARGET_FILE := $(ROOT)/.target
SUPPORTED_TARGETS := esp32c6 nrf52840
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
NRF52840_DIR := $(ROOT)/firmware/nrf52840
BUILD_ROOT := $(ROOT)/.build

WEST := $(ROOT)/.venv/bin/west
ZEPHYR_WEST_ROOT := $(ROOT)/firmware
ZEPHYR_WORKSPACE := $(ZEPHYR_WEST_ROOT)/.platformio/zephyr-workspace
ZEPHYR_BASE := $(ZEPHYR_WORKSPACE)/zephyr
ZEPHYR_SETUP_MARKER := $(ZEPHYR_WORKSPACE)/.setup-v4.2.0
ZEPHYR_BOARD ?= promicro_nrf52840/nrf52840/uf2
NRF52840_BUILD_DIR := $(NRF52840_DIR)/build
ARM_TOOLCHAIN_DIR := $(ROOT)/.platformio/packages/toolchain-gccarmnoneeabi
ARM_GCC := $(ARM_TOOLCHAIN_DIR)/bin/arm-none-eabi-gcc
ARM_TOOLCHAIN_MARKER := $(ARM_TOOLCHAIN_DIR)/.canon-remote-1.140201.0
CMAKE := $(ROOT)/.platformio/packages/tool-cmake/bin/cmake
CMAKE_MARKER := $(ROOT)/.platformio/packages/tool-cmake/.canon-remote-3.30.2
NINJA := $(ROOT)/.platformio/packages/tool-ninja/ninja
NINJA_MARKER := $(ROOT)/.platformio/packages/tool-ninja/.canon-remote-1.13.2
ZEPHYR_BUILD_ENV = PATH="$(dir $(CMAKE)):$(dir $(NINJA)):$(dir $(ARM_GCC)):$(ROOT)/.venv/bin:$$PATH" \
	ZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
	CROSS_COMPILE="$(ARM_TOOLCHAIN_DIR)/bin/arm-none-eabi-"

HOST_CC ?= cc
HOST_TEST := $(BUILD_ROOT)/host/test_canon_protocol

.PHONY: help target select setup build upload monitor serial test \
        compile-commands clean clean-all

help:
	@echo "Selected target: $(TARGET)"
	@echo ""
	@echo "  make select TARGET=esp32c6  Save the active target"
	@echo "  make select TARGET=nrf52840 Save the active target"
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
	uv pip install --python "$(PYTHON)" pip platformio==6.1.19 pyserial==3.5

$(WEST): $(PLATFORMIO)
	uv pip install --python "$(PYTHON)" west==1.4.0

$(ARM_TOOLCHAIN_MARKER): $(PLATFORMIO)
	@if test ! -x "$(ARM_GCC)"; then \
		$(PLATFORMIO) pkg install --global \
			--tool "platformio/toolchain-gccarmnoneeabi@1.140201.0"; \
	fi
	@touch "$@"

$(CMAKE_MARKER): $(PLATFORMIO)
	@if test ! -x "$(CMAKE)"; then \
		$(PLATFORMIO) pkg install --global \
			--tool "platformio/tool-cmake@3.30.2"; \
	fi
	@touch "$@"

$(NINJA_MARKER): $(PLATFORMIO)
	@if test ! -x "$(NINJA)"; then \
		$(PLATFORMIO) pkg install --global \
			--tool "platformio/tool-ninja@1.13.2"; \
	fi
	@touch "$@"

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
	$(PYTHON) tools/normalize_compile_commands.py \
		"$(ESP32C6_DIR)/compile_commands.json" \
		"$(ROOT)/.platformio/packages/toolchain-riscv32-esp/bin"
	ln -sfn "$(ESP32C6_DIR)/compile_commands.json" "$(ROOT)/compile_commands.json"

clean: setup
	$(PLATFORMIO) run --project-dir "$(ESP32C6_DIR)" --target clean

else

$(ZEPHYR_WEST_ROOT)/.west/config: $(WEST) $(NRF52840_DIR)/west.yml
	@if test ! -f "$@"; then \
		"$(WEST)" init -l "$(NRF52840_DIR)"; \
	fi

$(ZEPHYR_SETUP_MARKER): $(ZEPHYR_WEST_ROOT)/.west/config \
	$(NRF52840_DIR)/west.yml
	cd "$(ZEPHYR_WEST_ROOT)" && "$(WEST)" update
	uv pip install --python "$(PYTHON)" \
		-r "$(ZEPHYR_BASE)/scripts/requirements-base.txt"
	@mkdir -p "$(dir $@)"
	@touch "$@"

setup: $(PLATFORMIO) $(WEST) $(ARM_TOOLCHAIN_MARKER) $(CMAKE_MARKER) \
	$(NINJA_MARKER) \
	$(ZEPHYR_SETUP_MARKER)

build: setup
	cd "$(ZEPHYR_WEST_ROOT)" && $(ZEPHYR_BUILD_ENV) "$(WEST)" build \
		--build-dir "$(NRF52840_BUILD_DIR)" \
		--board "$(ZEPHYR_BOARD)" \
		--pristine=auto "$(NRF52840_DIR)"

upload: build
	"$(PYTHON)" tools/uf2_upload.py \
		"$(NRF52840_BUILD_DIR)/zephyr/zephyr.uf2"

monitor serial: setup
	SERIAL_TARGET=nrf52840 $(PYTHON) tools/serial_terminal.py $(ARGS)

compile-commands: build
	ln -sfn "$(NRF52840_BUILD_DIR)/compile_commands.json" \
		"$(ROOT)/compile_commands.json"

clean:
	@if test -f "$(NRF52840_BUILD_DIR)/build.ninja"; then \
		"$(CMAKE)" --build "$(NRF52840_BUILD_DIR)" --target clean; \
	fi

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
	$(RM) -r "$(BUILD_ROOT)" "$(ESP32C6_DIR)/.pio" \
		"$(NRF52840_BUILD_DIR)"
