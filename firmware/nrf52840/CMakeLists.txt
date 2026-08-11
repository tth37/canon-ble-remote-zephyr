cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nrf52840_canon_remote)

target_sources(app PRIVATE
    src/main.c
    src/canon_ble_zephyr.c
    src/shell_commands.c
    ../../shared/canon/src/canon_protocol.c
)

target_include_directories(app PRIVATE
    src
    ../../shared/canon/include
)

target_compile_options(app PRIVATE -Wall -Wextra -Werror)
