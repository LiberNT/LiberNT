# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Image section creation under a transient page shortage
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

set(IMAGE_SECTION_WAIT_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/imagesectionwait-generated.c)
add_custom_command(OUTPUT ${IMAGE_SECTION_WAIT_SOURCE}
    COMMAND ${CMAKE_COMMAND} -DNTSECTION_SOURCE=${MMCC_KERNEL_ROOT}/mm/nt/ntsection.c
            -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/imagesectionwait-fixture.c.in
            -DOUTPUT=${IMAGE_SECTION_WAIT_SOURCE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/make-imagesectionwait.cmake
    DEPENDS ${MMCC_KERNEL_ROOT}/mm/nt/ntsection.c ${CMAKE_CURRENT_SOURCE_DIR}/imagesectionwait-fixture.c.in
            ${CMAKE_CURRENT_SOURCE_DIR}/make-imagesectionwait.cmake
    VERBATIM)
add_executable(mm_imagesectionwait ${IMAGE_SECTION_WAIT_SOURCE})
add_test(NAME mm.imagesectionwait COMMAND mm_imagesectionwait)
