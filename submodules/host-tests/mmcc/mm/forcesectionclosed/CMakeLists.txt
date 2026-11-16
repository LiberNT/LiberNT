# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     MmForceSectionClosedEx closes the section kinds its flags name
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

set(FORCE_SECTION_CLOSED_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/forcesectionclosed-generated.c)
add_custom_command(OUTPUT ${FORCE_SECTION_CLOSED_SOURCE}
    COMMAND ${CMAKE_COMMAND} -DNTSECTION_SOURCE=${MMCC_KERNEL_ROOT}/mm/nt/ntsection.c
            -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/forcesectionclosed-fixture.c.in
            -DOUTPUT=${FORCE_SECTION_CLOSED_SOURCE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/make-forcesectionclosed.cmake
    DEPENDS ${MMCC_KERNEL_ROOT}/mm/nt/ntsection.c ${CMAKE_CURRENT_SOURCE_DIR}/forcesectionclosed-fixture.c.in
            ${CMAKE_CURRENT_SOURCE_DIR}/make-forcesectionclosed.cmake
    VERBATIM)
add_executable(mm_forcesectionclosed ${FORCE_SECTION_CLOSED_SOURCE})
add_test(NAME mm.forcesectionclosed COMMAND mm_forcesectionclosed)
