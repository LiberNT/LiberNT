# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Boot loader host-test build configuration
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME bootloader.gop_scan_line COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gop-scanline-test.py
        --root ${REACTOS_HOST_SOURCE_ROOT} --cc ${CMAKE_C_COMPILER})
    set_tests_properties(bootloader.gop_scan_line PROPERTIES TIMEOUT 60)
    if(NOT CMAKE_HOST_WIN32)
        add_test(NAME bootloader.wim_media_list COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/wim-media-list-test.py
            --root ${REACTOS_HOST_SOURCE_ROOT} --cmake ${CMAKE_COMMAND})
        set_tests_properties(bootloader.wim_media_list PROPERTIES TIMEOUT 60)
    endif()
endif()
