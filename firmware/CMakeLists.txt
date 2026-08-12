cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(canon_remote)

target_sources(app PRIVATE
    src/main.c
    src/canon/protocol.c
    src/canon/remote.c
    src/hardware/controls.c
    src/hardware/display.c
    src/hardware/indicator.c
    src/hardware/power.c
)

target_sources_ifdef(CONFIG_SHELL app PRIVATE src/shell_commands.c)

target_include_directories(app PRIVATE
    src
)

target_compile_options(app PRIVATE -Wall -Wextra -Werror)
