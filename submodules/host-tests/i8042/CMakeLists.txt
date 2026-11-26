# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     PS/2 controller host regression test
# COPYRIGHT:   Copyright 2026 LiberNT contributors

add_executable(i8042_port port.c ${REACTOS_HOST_SOURCE_ROOT}/drivers/input/i8042prt/readwrite.c
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/input/i8042prt/packet.c
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/input/i8042prt/sync_core.c)
target_include_directories(i8042_port PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(i8042_port PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/shim.h -Wall -Wextra)
add_test(NAME i8042.port COMMAND i8042_port)
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME i8042.keyboard_overflow
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/keyboard-overflow-test.py
            --root ${REACTOS_HOST_SOURCE_ROOT} --cc ${CMAKE_C_COMPILER})
    add_test(NAME i8042.keyboard_transfer
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/keyboard-transfer-test.py
            --root ${REACTOS_HOST_SOURCE_ROOT} --cc ${CMAKE_C_COMPILER})
    add_test(NAME i8042.mouse_partial
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/mouse-partial-test.py
            --root ${REACTOS_HOST_SOURCE_ROOT} --cc ${CMAKE_C_COMPILER})
    add_test(NAME input.class_cancelled_read
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/class-cancelled-read-test.py
            --root ${REACTOS_HOST_SOURCE_ROOT} --cc ${CMAKE_C_COMPILER})
    add_test(NAME input.kbdhid_consumed
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/kbdhid-consumed-test.py
            --root ${REACTOS_HOST_SOURCE_ROOT} --cc ${CMAKE_C_COMPILER})
endif()
