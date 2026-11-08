# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Paging-file initialization regression
# COPYRIGHT:   Copyright 2026 LiberNT contributors

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME mm.pagefile_init COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/pagefile-init-test.py
            --source ${MMCC_KERNEL_ROOT}/mm/nt/ntpagefile.c --init ${MMCC_KERNEL_ROOT}/mm/nt/ntinit.c
            --cc ${CMAKE_C_COMPILER})
        add_test(NAME mm.pagefile_growth COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/pagefile-growth-test.py
            --source ${MMCC_KERNEL_ROOT}/mm/nt/ntpagefile.c --cc ${CMAKE_C_COMPILER})
        option(MM_PAGEFILE_TSAN "Run the paging-file backoff ThreadSanitizer regression" OFF)
        if(MM_PAGEFILE_TSAN)
            add_test(NAME mm.pagefile_backoff_tsan COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/pagefile-growth-test.py
                --source ${MMCC_KERNEL_ROOT}/mm/nt/ntpagefile.c --cc ${CMAKE_C_COMPILER} --backoff --tsan)
        endif()
    endif()
endif()
