# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     TCP/IP driver host regression tests
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME tcpip.receive_no_memory COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/receive-no-memory-test.py
        --root ${REACTOS_HOST_SOURCE_ROOT} --cc ${CMAKE_C_COMPILER})
    add_test(NAME tcpip.associate_bad_address_file COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/associate-bad-address-file-test.py
        --root ${REACTOS_HOST_SOURCE_ROOT} --cc ${CMAKE_C_COMPILER})
endif()
