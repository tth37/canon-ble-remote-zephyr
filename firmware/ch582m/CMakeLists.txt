cmake_minimum_required(VERSION 3.22)

if(NOT CH58X_SDK_ROOT)
    message(FATAL_ERROR "CH58X_SDK_ROOT must point to the pinned OpenWCH SDK")
endif()
if(NOT CH58X_TOOLCHAIN_BIN)
    message(FATAL_ERROR "CH58X_TOOLCHAIN_BIN must point to the RISC-V toolchain")
endif()

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_COMPILER "${CH58X_TOOLCHAIN_BIN}/riscv32-esp-elf-gcc")
set(CMAKE_ASM_COMPILER "${CH58X_TOOLCHAIN_BIN}/riscv32-esp-elf-gcc")
set(CMAKE_OBJCOPY "${CH58X_TOOLCHAIN_BIN}/riscv32-esp-elf-objcopy")
set(CMAKE_SIZE "${CH58X_TOOLCHAIN_BIN}/riscv32-esp-elf-size")
set(CMAKE_EXECUTABLE_SUFFIX ".elf")

project(ch582m_canon_remote C ASM)

set(SDK_SOURCE "${CH58X_SDK_ROOT}/EVT/EXAM/SRC")
set(SDK_BLE "${CH58X_SDK_ROOT}/EVT/EXAM/BLE")
set(COMMON_CANON "${CMAKE_CURRENT_LIST_DIR}/../../shared/canon")

set(VENDOR_SOURCES
    "${SDK_BLE}/HAL/MCU.c"
    "${SDK_BLE}/HAL/RTC.c"
    "${SDK_BLE}/HAL/SLEEP.c"
    "${SDK_SOURCE}/RVMSIS/core_riscv.c"
    "${SDK_SOURCE}/Startup/startup_CH583.S"
    "${SDK_SOURCE}/StdPeriphDriver/CH58x_adc.c"
    "${SDK_SOURCE}/StdPeriphDriver/CH58x_clk.c"
    "${SDK_SOURCE}/StdPeriphDriver/CH58x_flash.c"
    "${SDK_SOURCE}/StdPeriphDriver/CH58x_gpio.c"
    "${SDK_SOURCE}/StdPeriphDriver/CH58x_pwr.c"
    "${SDK_SOURCE}/StdPeriphDriver/CH58x_sys.c"
    "${SDK_SOURCE}/StdPeriphDriver/CH58x_uart1.c"
)

add_executable(ch582m_canon_remote
    src/main.c
    src/newlib_hooks.c
    src/serial_console.c
    src/canon_ble_wch.c
    "${COMMON_CANON}/src/canon_protocol.c"
    ${VENDOR_SOURCES}
)
set_target_properties(ch582m_canon_remote PROPERTIES SUFFIX ".elf")

target_include_directories(ch582m_canon_remote PRIVATE
    include
    "${COMMON_CANON}/include"
    "${SDK_BLE}/HAL/include"
    "${SDK_BLE}/LIB"
    "${SDK_SOURCE}/RVMSIS"
    "${SDK_SOURCE}/StdPeriphDriver/inc"
)

target_compile_definitions(ch582m_canon_remote PRIVATE
    DEBUG=1
    INT_SOFT=1
)
target_compile_options(ch582m_canon_remote PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-include>"
    "$<$<COMPILE_LANGUAGE:C>:CONFIG.h>"
    -march=rv32imac_zicsr_zifencei
    -mabi=ilp32
    -Os
    -ffunction-sections
    -fdata-sections
    -fsigned-char
    -fno-common
    -std=gnu11
    -Wall
    -Wextra
    -Werror
)

# Vendor sources contain legacy constructs and WCH-specific interrupt metadata.
# INT_SOFT selects standard RISC-V interrupt entry; silence warnings only there.
set_source_files_properties(${VENDOR_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

target_link_options(ch582m_canon_remote PRIVATE
    -march=rv32imac_zicsr_zifencei
    -mabi=ilp32
    -nostartfiles
    --specs=nano.specs
    --specs=nosys.specs
    "-T${SDK_SOURCE}/Ld/Link.ld"
    -Wl,--gc-sections
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/ch582m_canon_remote.map"
)
target_link_libraries(ch582m_canon_remote PRIVATE
    "${SDK_BLE}/LIB/LIBCH58xBLE.a"
    "${SDK_SOURCE}/StdPeriphDriver/libISP583.a"
    c
    gcc
)

add_custom_command(TARGET ch582m_canon_remote POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O ihex
            "$<TARGET_FILE:ch582m_canon_remote>"
            "${CMAKE_CURRENT_BINARY_DIR}/ch582m_canon_remote.hex"
    COMMAND "${CMAKE_OBJCOPY}" -O binary
            "$<TARGET_FILE:ch582m_canon_remote>"
            "${CMAKE_CURRENT_BINARY_DIR}/ch582m_canon_remote.bin"
    COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:ch582m_canon_remote>"
    VERBATIM
)
