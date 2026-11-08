# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Bootstrap allocator regression tests
# COPYRIGHT:   Copyright 2026 LiberNT contributors

find_package(Threads REQUIRED)
add_executable(nvs_bootalloc bootalloc.c ${MMCC_KERNEL_ROOT}/nvs/nt/ntglobal.c)
target_include_directories(nvs_bootalloc BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nvs_bootalloc PRIVATE Threads::Threads)
foreach(CASE oversize exhaust rollback concurrent pool)
    add_test(NAME nvs_bootalloc.${CASE} COMMAND nvs_bootalloc ${CASE})
endforeach()
