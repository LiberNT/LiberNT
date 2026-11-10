# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     MDL cache-policy regression tests
# COPYRIGHT:   Copyright 2026 LiberNT contributors

set(MDL_CACHE_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/mdlcache-generated.c)
add_custom_command(OUTPUT ${MDL_CACHE_SOURCE}
    COMMAND ${CMAKE_COMMAND} -DNTMDL_SOURCE=${MMCC_KERNEL_ROOT}/mm/nt/ntmdl.c
            -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/mdlcache-fixture.c.in
            -DOUTPUT=${MDL_CACHE_SOURCE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/make-mdlcache.cmake
    DEPENDS ${MMCC_KERNEL_ROOT}/mm/nt/ntmdl.c ${CMAKE_CURRENT_SOURCE_DIR}/mdlcache-fixture.c.in
            ${CMAKE_CURRENT_SOURCE_DIR}/make-mdlcache.cmake
    VERBATIM)
add_executable(mm_mdlcache ${MDL_CACHE_SOURCE})
add_test(NAME mm.mdlcache COMMAND mm_mdlcache)
