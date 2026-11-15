# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     User-mode MDL mapping under a transient page shortage
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

set(MDL_MAP_USER_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/mdlmapuser-generated.c)
add_custom_command(OUTPUT ${MDL_MAP_USER_SOURCE}
    COMMAND ${CMAKE_COMMAND} -DNTMDL_SOURCE=${MMCC_KERNEL_ROOT}/mm/nt/ntmdl.c
            -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/mdlmapuser-fixture.c.in
            -DOUTPUT=${MDL_MAP_USER_SOURCE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/make-mdlmapuser.cmake
    DEPENDS ${MMCC_KERNEL_ROOT}/mm/nt/ntmdl.c ${CMAKE_CURRENT_SOURCE_DIR}/mdlmapuser-fixture.c.in
            ${CMAKE_CURRENT_SOURCE_DIR}/make-mdlmapuser.cmake
    VERBATIM)
add_executable(mm_mdlmapuser ${MDL_MAP_USER_SOURCE})
add_test(NAME mm.mdlmapuser COMMAND mm_mdlmapuser)
