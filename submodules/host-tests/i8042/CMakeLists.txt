# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     PS/2 controller host regression test
# COPYRIGHT:   Copyright 2026 LiberNT contributors

add_executable(i8042_port port.c ${REACTOS_HOST_SOURCE_ROOT}/drivers/input/i8042prt/readwrite.c
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/input/i8042prt/packet.c)
target_include_directories(i8042_port PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(i8042_port PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/shim.h -Wall -Wextra)
add_test(NAME i8042.port COMMAND i8042_port)
