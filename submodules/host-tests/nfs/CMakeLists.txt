# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     NFS redirector host regression tests
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME nfs.enable_caching COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/enable-caching-test.py
        --root ${REACTOS_HOST_SOURCE_ROOT} --cc ${CMAKE_C_COMPILER})
endif()
