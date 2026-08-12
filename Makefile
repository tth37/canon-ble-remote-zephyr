ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
PROJECT_CONFIG := $(ROOT)/.config
DEFAULT_BOARD := esp32c6_devkitc/esp32c6/hpcore
SUPPORTED_BOARDS := \
	esp32c6_devkitc/esp32c6/hpcore \
	promicro_nrf52840/nrf52840/uf2
CONFIGURED_BOARD := $(strip $(shell \
	if test -f "$(PROJECT_CONFIG)" && \
		grep -q '^CONFIG_PROJECT_BOARD_NRF52840=y$$' "$(PROJECT_CONFIG)"; then \
		printf '%s' 'promicro_nrf52840/nrf52840/uf2'; \
	elif test -f "$(PROJECT_CONFIG)" && \
		grep -q '^CONFIG_PROJECT_BOARD_ESP32C6=y$$' "$(PROJECT_CONFIG)"; then \
		printf '%s' 'esp32c6_devkitc/esp32c6/hpcore'; \
	fi))
CONFIGURED_PROFILE := $(strip $(shell \
	if test -f "$(PROJECT_CONFIG)" && \
		grep -q '^CONFIG_PROJECT_PROFILE_RELEASE=y$$' "$(PROJECT_CONFIG)"; then \
		printf '%s' 'release'; \
	elif test -f "$(PROJECT_CONFIG)" && \
		grep -q '^CONFIG_PROJECT_PROFILE_DEBUG=y$$' "$(PROJECT_CONFIG)"; then \
		printf '%s' 'debug'; \
	fi))
BOARD ?= $(if $(CONFIGURED_BOARD),$(CONFIGURED_BOARD),$(DEFAULT_BOARD))
PROFILE ?= $(if $(CONFIGURED_PROFILE),$(CONFIGURED_PROFILE),debug)
SUPPORTED_PROFILES := debug release

ifeq ($(filter $(BOARD),$(SUPPORTED_BOARDS)),)
$(error Unsupported BOARD '$(BOARD)'; choose one of: $(SUPPORTED_BOARDS))
endif

ifeq ($(filter $(PROFILE),$(SUPPORTED_PROFILES)),)
$(error Unsupported PROFILE '$(PROFILE)'; choose one of: $(SUPPORTED_PROFILES))
endif

ifeq ($(PROFILE),release)
ifneq ($(BOARD),esp32c6_devkitc/esp32c6/hpcore)
$(error PROFILE=release currently supports only esp32c6_devkitc/esp32c6/hpcore)
endif
endif

PROFILE_BUILD_SUFFIX := _$(PROFILE)
PROFILE_CMAKE_ARGS := -- -DFILE_SUFFIX=$(PROFILE)

PYTHON := $(ROOT)/.venv/bin/python
WEST := $(ROOT)/.venv/bin/west
TOOLS_MARKER := $(ROOT)/.venv/.zephyr-tools-v2
export PATH := $(ROOT)/.venv/bin:$(PATH)
WEST_CONFIG := $(ROOT)/.west/config
ZEPHYR_BASE := $(ROOT)/.zephyr/zephyr
ZEPHYR_PATCH := $(ROOT)/patches/zephyr/0001-canon-fixed-identity.patch
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
BUILD_DIR := $(BUILD_ROOT)/$(BOARD_KEY)$(PROFILE_BUILD_SUFFIX)

.PHONY: help board selection menuconfig zephyr-menuconfig setup patch-zephyr \
	build upload flash serial compile-commands clean pristine clean-all

help:
	@echo "Zephyr Canon BLE remote"
	@echo ""
	@echo "Selected BOARD: $(BOARD)"
	@echo "Build PROFILE:  $(PROFILE)"
	@echo ""
	@echo "  make menuconfig            Select the board and build profile"
	@echo "  make setup                 Install West, Zephyr, SDK, and blobs locally"
	@echo "  make build                 Run west build for the active board"
	@echo "  make zephyr-menuconfig     Configure the selected Zephyr build"
	@echo "  make upload                Run west flash for the active board"
	@echo "  make serial                Open the interactive serial shell"
	@echo "  make compile-commands      Refresh editor compile commands"
	@echo "  make pristine              Remove the active Zephyr build directory"
	@echo ""
	@echo "Supported boards:"
	@$(foreach item,$(SUPPORTED_BOARDS),echo "  $(item)";)

board:
	@echo "$(BOARD)"

selection:
	@echo "BOARD=$(BOARD)"
	@echo "PROFILE=$(PROFILE)"

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

patch-zephyr: $(ZEPHYR_PROJECTS_MARKER)
	@if git -C "$(ZEPHYR_BASE)" apply --reverse --check \
		"$(ZEPHYR_PATCH)" >/dev/null 2>&1; then \
		echo "Zephyr Canon identity patch already applied"; \
	elif git -C "$(ZEPHYR_BASE)" apply --check "$(ZEPHYR_PATCH)"; then \
		git -C "$(ZEPHYR_BASE)" apply "$(ZEPHYR_PATCH)"; \
		echo "Applied Zephyr Canon identity patch"; \
	else \
		echo "Pinned Zephyr source does not match the Canon identity patch" >&2; \
		exit 1; \
	fi

setup: $(ZEPHYR_PACKAGES_MARKER) $(ZEPHYR_BLOBS_MARKER) \
	$(ZEPHYR_SDK_MARKER) patch-zephyr

menuconfig: $(ZEPHYR_PROJECTS_MARKER)
	KCONFIG_CONFIG="$(PROJECT_CONFIG)" \
		$(PYTHON) "$(ZEPHYR_BASE)/scripts/kconfig/menuconfig.py" \
		"$(ROOT)/Kconfig.project"
	@$(MAKE) --no-print-directory selection

build: setup
	$(WEST) build --build-dir "$(BUILD_DIR)" --board "$(BOARD)" \
		--pristine=auto "$(ROOT)/firmware" $(PROFILE_CMAKE_ARGS)

zephyr-menuconfig: build
	$(WEST) build --build-dir "$(BUILD_DIR)" -t menuconfig

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

clean-all:
	$(RM) -r "$(BUILD_ROOT)"
