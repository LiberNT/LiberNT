# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Firmware table validation host regression test
# COPYRIGHT:   Copyright 2026 LiberNT contributors

add_executable(acpival_table test.c
    ${REACTOS_HOST_SOURCE_ROOT}/hal/arch/common/acpi/acpival.c
    ${REACTOS_HOST_SOURCE_ROOT}/hal/arch/common/acpi/acpifault.c)
target_include_directories(acpival_table PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${REACTOS_HOST_SOURCE_ROOT}/hal/arch/common/include
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/include)
target_compile_options(acpival_table PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/shim.h -Wall -Wextra)
add_test(NAME acpival.table COMMAND acpival_table)
