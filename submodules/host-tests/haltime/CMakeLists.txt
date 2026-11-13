# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Stall end tick arithmetic host regression test
# COPYRIGHT:   Copyright 2026 LiberNT contributors

add_executable(haltime_stall test.c)
target_include_directories(haltime_stall PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${REACTOS_HOST_SOURCE_ROOT}/hal/halx86/apic)
target_compile_options(haltime_stall PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/shim.h -Wall -Wextra)
add_test(NAME haltime.stall COMMAND haltime_stall)
