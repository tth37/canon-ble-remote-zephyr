ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BOARD_FILE := $(ROOT)/.board
DEFAULT_BOARD := esp32c6_devkitc/esp32c6/hpcore
SUPPORTED_BOARDS := \
	esp32c6_devkitc/esp32c6/hpcore \
	promicro_nrf52840/nrf52840/uf2
SAVED_BOARD := $(strip $(shell test -f "$(BOARD_FILE)" && sed -n '1p' "$(BOARD_FILE)"))
BOARD ?= $(if $(SAVED_BOARD),$(SAVED_BOARD),$(DEFAULT_BOARD))

ifeq ($(filter $(BOARD),$(SUPPORTED_BOARDS)),)
$(error Unsupported BOARD '$(BOARD)'; choose one of: $(SUPPORTED_BOARDS))
endif

PYTHON := $(ROOT)/.venv/bin/python
WEST := $(ROOT)/.venv/bin/west
TOOLS_MARKER := $(ROOT)/.venv/.zephyr-tools-v2
export PATH := $(ROOT)/.venv/bin:$(PATH)
WEST_CONFIG := $(ROOT)/.west/config
ZEPHYR_BASE := $(ROOT)/.zephyr/zephyr
ZEPHYR_VERSION := 4.2.0
ZEPHYR_SDK_VERSION := 0.17.2
ZEPHYR_PROJECTS_MARKER := $(ROOT)/.zephyr/.updated-v$(ZEPHYR_VERSION)
ZEPHYR_PACKAGES_MARKER := $(ROOT)/.venv/.zephyr-packages-v$(ZEPHYR_VERSION)
ZEPHYR_BLOBS_MARKER := $(ROOT)/.zephyr/.hal-espressif-blobs-v$(ZEPHYR_VERSION)
ZEPHYR_SDK_INSTALL_DIR := $(ROOT)/.zephyr-sdk
ZEPHYR_SDK_MARKER := $(ZEPHYR_SDK_INSTALL_DIR)/.installed-sdk-$(ZEPHYR_SDK_VERSION)
export ZEPHYR_BASE ZEPHYR_SDK_INSTALL_DIR

BOARD_KEY := $(subst /,_,$(BOARD))
BUILD_ROOT := $(ROOT)/.build
BUILD_DIR := $(BUILD_ROOT)/$(BOARD_KEY)
HOST_CC ?= cc
HOST_TEST := $(BUILD_ROOT)/host/test_canon_protocol

.PHONY: help board select setup build upload flash serial test \
	compile-commands clean pristine clean-all

help:
	@echo "Zephyr Canon BLE remote"
	@echo ""
	@echo "Selected BOARD: $(BOARD)"
	@echo ""
	@echo "  make select BOARD=<board>  Save the active Zephyr board"
	@echo "  make setup                 Install West, Zephyr, SDK, and blobs locally"
	@echo "  make build                 Run west build for the active board"
	@echo "  make upload                Run west flash for the active board"
	@echo "  make serial                Open the interactive serial shell"
	@echo "  make test                  Run portable Canon protocol tests"
	@echo "  make compile-commands      Refresh editor compile commands"
	@echo "  make pristine              Remove the active Zephyr build directory"
	@echo ""
	@echo "Supported boards:"
	@$(foreach item,$(SUPPORTED_BOARDS),echo "  $(item)";)

board:
	@echo "$(BOARD)"

select:
	@printf '%s\n' "$(BOARD)" > "$(BOARD_FILE)"
	@echo "Selected Zephyr board: $(BOARD)"

$(PYTHON):
	uv venv --python 3.13 "$(ROOT)/.venv"

$(TOOLS_MARKER): $(PYTHON) requirements-tools.txt
	uv pip install --python "$(PYTHON)" \
		-r "$(ROOT)/requirements-tools.txt"
	touch "$@"

$(WEST_CONFIG): $(TOOLS_MARKER) firmware/west.yml
	@if test ! -f "$(WEST_CONFIG)"; then \
		$(WEST) init -l "$(ROOT)/firmware"; \
	fi

$(ZEPHYR_PROJECTS_MARKER): $(WEST_CONFIG) firmware/west.yml
	$(WEST) update
	touch "$@"

$(ZEPHYR_PACKAGES_MARKER): $(ZEPHYR_PROJECTS_MARKER)
	$(WEST) packages pip --install
	touch "$@"

$(ZEPHYR_BLOBS_MARKER): $(ZEPHYR_PROJECTS_MARKER)
	$(WEST) blobs fetch hal_espressif
	touch "$@"

$(ZEPHYR_SDK_MARKER): $(ZEPHYR_PROJECTS_MARKER)
	$(WEST) sdk install --install-dir "$(ZEPHYR_SDK_INSTALL_DIR)" \
		--toolchains arm-zephyr-eabi riscv64-zephyr-elf
	touch "$@"

setup: $(ZEPHYR_PACKAGES_MARKER) $(ZEPHYR_BLOBS_MARKER) \
	$(ZEPHYR_SDK_MARKER)

build: setup
	$(WEST) build --build-dir "$(BUILD_DIR)" --board "$(BOARD)" \
		--pristine=auto "$(ROOT)/firmware"

upload flash: build
	$(WEST) flash --build-dir "$(BUILD_DIR)" $(FLASH_ARGS)

serial: $(TOOLS_MARKER)
	ZEPHYR_BOARD="$(BOARD)" $(PYTHON) tools/serial_terminal.py $(ARGS)

compile-commands: build
	ln -sfn "$(BUILD_DIR)/compile_commands.json" \
		"$(ROOT)/compile_commands.json"

clean: setup
	$(WEST) build --build-dir "$(BUILD_DIR)" -t clean

pristine:
	$(RM) -r "$(BUILD_DIR)"

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
	$(PYTHON) tests/host/test_serial_terminal.py

clean-all:
	$(RM) -r "$(BUILD_ROOT)"
