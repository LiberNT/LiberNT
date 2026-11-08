# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Memory manager atomic ordering regression
# COPYRIGHT:   Copyright 2026 LiberNT contributors

set(MM_ARM64_COMPDB "" CACHE FILEPATH "ARM64 kernel compile database for ordering tests")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND EXISTS "${MM_ARM64_COMPDB}")
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME mm.atomic_pointer_acquire COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/atomic-pointer-test.py --compdb ${MM_ARM64_COMPDB})
    endif()
endif()
